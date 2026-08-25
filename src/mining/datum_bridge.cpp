// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
#include <mining/datum_bridge.h>

#include <bitcoin-build-config.h>
#include <addresstype.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <clientversion.h>
#include <common/args.h>
#include <common/netif.h>
#include <common/pcp.h>
#include <key_io.h>
#include <logging.h>
#include <netbase.h>
#include <node/context.h>
#include <random.h>
#include <rpc/request.h>
#include <tinyformat.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <util/thread.h>
#include <util/threadinterrupt.h>
#include <util/translation.h>
#include <validationinterface.h>

#ifdef USE_UPNP
#include <cstddef> // Work around missing include in miniupnpc 2.3.3.
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>
static_assert(MINIUPNPC_API_VERSION >= 17, "miniUPnPc API version >= 17 assumed");
#endif // USE_UPNP

extern "C" {
#include <datum_embedded.h>
#include <datum_logger.h>
}

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace mining {
namespace {

constexpr int64_t DEFAULT_DATUM_PORT{23334};
constexpr int64_t DEFAULT_DATUM_MAX_CLIENTS{32};
constexpr int64_t DEFAULT_DATUM_MAX_PER_IP{4};
constexpr int64_t DEFAULT_DATUM_DIFFICULTY{65536};
constexpr int64_t DEFAULT_DATUM_AUTH_TIMEOUT{10};
constexpr int MAX_DATUM_DIFFICULTY{2147483647};
constexpr bool DEFAULT_DATUM_AUTH{false};
constexpr bool DEFAULT_DATUM_UPNP{false};

struct DatumRuntimeState {
    bool enabled{false};
    std::string listen{"127.0.0.1"};
    uint16_t port{DEFAULT_DATUM_PORT};
    uint64_t share_difficulty{DEFAULT_DATUM_DIFFICULTY};
    bool upnp{DEFAULT_DATUM_UPNP};
    node::NodeContext* node{nullptr};
};

DatumRuntimeState g_datum_state;

class DatumValidationInterface final : public CValidationInterface
{
    void UpdatedBlockTip(const CBlockIndex* new_tip, const CBlockIndex* fork_tip, bool initial_download) override
    {
        if (!initial_download && new_tip && new_tip != fork_tip) datum_request_template_refresh();
    }
};

std::unique_ptr<DatumValidationInterface> g_datum_validation;

#ifdef USE_UPNP
using namespace std::chrono_literals;
static constexpr auto DATUM_UPNP_REANNOUNCE_PERIOD{20min};
static constexpr auto DATUM_UPNP_RETRY_PERIOD{5min};
static CThreadInterrupt g_datum_upnp_interrupt;
static std::thread g_datum_upnp_thread;

const char* DatumMappingErrorName(const MappingError error)
{
    switch (error) {
    case MappingError::NETWORK_ERROR: return "network error";
    case MappingError::PROTOCOL_ERROR: return "protocol error";
    case MappingError::UNSUPP_VERSION: return "unsupported protocol";
    case MappingError::NO_RESOURCES: return "no resources";
    }
    return "unknown error";
}

std::optional<MappingResult> ProcessDatumPcpOrNatpmp(const uint16_t port, const uint32_t lifetime, PCPMappingNonce& nonce)
{
    const auto gateway{QueryDefaultGateway(NET_IPV4)};
    if (!gateway) {
        LogPrintf("[datum] PCP/NAT-PMP could not determine the IPv4 default gateway\n");
        return std::nullopt;
    }

    const auto bind_address{LookupHost("0.0.0.0", /*fAllowLookup=*/false)};
    if (!bind_address) {
        LogPrintf("[datum] PCP/NAT-PMP could not prepare the IPv4 bind address\n");
        return std::nullopt;
    }

    auto result{PCPRequestPortMap(nonce, *gateway, *bind_address, port, lifetime, g_datum_upnp_interrupt)};
    if (const auto* error = std::get_if<MappingError>(&result); error && *error == MappingError::UNSUPP_VERSION) {
        LogPrintf("[datum] PCP is unsupported; falling back to NAT-PMP\n");
        result = NATPMPRequestPortMap(*gateway, port, lifetime, g_datum_upnp_interrupt);
    }
    if (const auto* mapping = std::get_if<MappingResult>(&result)) return *mapping;

    if (const auto* error = std::get_if<MappingError>(&result)) {
        LogPrintf("[datum] PCP/NAT-PMP mapping failed: %s (%d)\n", DatumMappingErrorName(*error), static_cast<int>(*error));
    }
    return std::nullopt;
}

bool ProcessDatumPcpOrNatpmpLoop(const uint16_t port)
{
    PCPMappingNonce nonce;
    GetRandBytes(nonce);
    const uint32_t requested_lifetime{static_cast<uint32_t>(std::chrono::seconds(DATUM_UPNP_REANNOUNCE_PERIOD * 2).count())};

    while (!g_datum_upnp_interrupt) {
        const auto mapping{ProcessDatumPcpOrNatpmp(port, requested_lifetime, nonce)};
        if (!mapping) return false;

        LogPrintf("[datum] mapped Stratum TCP port via %s: %s\n",
                  mapping->version == 0 ? "NAT-PMP" : "PCP", mapping->ToString());
        if (mapping->lifetime < 30) {
            LogPrintf("[datum] PCP/NAT-PMP returned an impossibly short mapping lifetime of %u seconds\n", mapping->lifetime);
            return false;
        }

        const std::chrono::seconds sleep_time{std::max<uint32_t>(mapping->lifetime / 2, 1U)};
        if (!g_datum_upnp_interrupt.sleep_for(sleep_time)) return true;
    }
    return true;
}

bool ProcessDatumUpnp(const uint16_t port)
{
    if (g_datum_upnp_interrupt) return false;

    const std::string port_string{strprintf("%u", port)};
    int error{0};
    UPNPDev* devlist{upnpDiscover(2000, nullptr, nullptr, 0, 0, 2, &error)};
    if (!devlist) {
        LogPrintf("[datum] UPnP discovery found no devices (error %d)\n", error);
        return false;
    }

    UPNPUrls urls{};
    IGDdatas data{};
    char lanaddr[64]{};
#if MINIUPNPC_API_VERSION <= 17
    const int valid_igd{UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr))};
#else
    const int valid_igd{UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr), nullptr, 0)};
#endif
    if (valid_igd != 1) {
        LogPrintf("[datum] no valid UPnP IGD found\n");
        freeUPNPDevlist(devlist);
        if (valid_igd != 0) FreeUPNPUrls(&urls);
        return false;
    }

    bool mapped{false};
    do {
        if (g_datum_upnp_interrupt) break;
        const int result{UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
                                              port_string.c_str(), port_string.c_str(), lanaddr,
                                              CLIENT_NAME " DATUM Stratum", "TCP", nullptr, "0")};
        if (result != UPNPCOMMAND_SUCCESS) {
            LogPrintf("[datum] UPnP AddPortMapping(%s, %s, %s) failed with code %d (%s)\n",
                      port_string, port_string, lanaddr, result, strupnperror(result));
            break;
        }
        mapped = true;
        LogPrintf("[datum] UPnP mapped Stratum TCP port %s\n", port_string);
    } while (g_datum_upnp_interrupt.sleep_for(DATUM_UPNP_REANNOUNCE_PERIOD));

    if (mapped) {
        const int result{UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, port_string.c_str(), "TCP", nullptr)};
        LogPrintf("[datum] UPnP DeletePortMapping(%s) returned %d\n", port_string, result);
    }
    freeUPNPDevlist(devlist);
    FreeUPNPUrls(&urls);
    return mapped;
}

void ThreadDatumUpnp(const uint16_t port)
{
    while (!g_datum_upnp_interrupt) {
        if (!ProcessDatumUpnp(port)) {
            // The node's default port mapping may use NAT-PMP/PCP even when
            // miniupnpc cannot discover an IGD. Try the same protocols for
            // DATUM before waiting for the next discovery cycle.
            if (ProcessDatumPcpOrNatpmpLoop(port)) break;
        }
        if (!g_datum_upnp_interrupt.sleep_for(DATUM_UPNP_RETRY_PERIOD)) break;
    }
}

void StartDatumUpnp(const uint16_t port)
{
    if (g_datum_upnp_thread.joinable()) return;
    g_datum_upnp_interrupt.reset();
    g_datum_upnp_thread = std::thread(&util::TraceThread, "datum-upnp", [port] { ThreadDatumUpnp(port); });
}

void StopDatumUpnp()
{
    g_datum_upnp_interrupt();
    if (g_datum_upnp_thread.joinable()) g_datum_upnp_thread.join();
    g_datum_upnp_interrupt.reset();
}
#else
void StartDatumUpnp(uint16_t) {}
void StopDatumUpnp() {}
#endif // USE_UPNP

bool IsLoopbackRpcUrl(const std::string& url)
{
    constexpr std::string_view prefix{"http://"};
    if (!url.starts_with(prefix)) return false;
    std::string authority{url.substr(prefix.size())};
    if (authority.ends_with('/')) authority.pop_back();
    if (authority.find_first_of("/?#@") != std::string::npos) return false;
    uint16_t port{0};
    std::string host;
    return SplitHostPort(authority, port, host) && port != 0 &&
        (host == "127.0.0.1" || host == "::1");
}

bool GetCredentialPair(const ArgsManager& args, std::string& user, std::string& password, bilingual_str& error)
{
    const bool datum_user_set{args.IsArgSet("-datumrpcuser")};
    const bool datum_password_set{args.IsArgSet("-datumrpcpassword")};
    if (datum_user_set != datum_password_set) {
        error = Untranslated("-datumrpcuser and -datumrpcpassword must be configured together");
        return false;
    }
    if (datum_user_set) {
        user = args.GetArg("-datumrpcuser", "");
        password = args.GetArg("-datumrpcpassword", "");
        if (user.empty() || password.empty()) {
            error = Untranslated("DATUM RPC credentials must not be empty");
            return false;
        }
        return true;
    }

    const bool rpc_user_set{args.IsArgSet("-rpcuser")};
    const bool rpc_password_set{args.IsArgSet("-rpcpassword")};
    if (rpc_user_set != rpc_password_set) {
        error = Untranslated("-rpcuser and -rpcpassword must both be set for DATUM fallback");
        return false;
    }
    if (rpc_user_set) {
        user = args.GetArg("-rpcuser", "");
        password = args.GetArg("-rpcpassword", "");
        if (user.empty() || password.empty()) {
            error = Untranslated("RPC fallback credentials for DATUM must not be empty");
            return false;
        }
        return true;
    }

    std::string cookie;
    if (!GetAuthCookie(&cookie)) {
        error = Untranslated("DATUM requires explicit RPC credentials or a readable RPC cookie");
        return false;
    }
    const auto separator{cookie.find(':')};
    if (separator == std::string::npos || separator == 0 || separator + 1 == cookie.size()) {
        error = Untranslated("DATUM could not parse the RPC authentication cookie");
        return false;
    }
    user = cookie.substr(0, separator);
    password = cookie.substr(separator + 1);
    return true;
}

} // namespace

void SetupDatumArgs(ArgsManager& argsman)
{
    argsman.AddArg("-datum", "Enable embedded DATUM/Stratum solo mining (default: 0)", ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    argsman.AddArg("-datumlisten=<addr>", "Address for embedded Stratum V1 to listen on (default: 127.0.0.1)", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::BLOCK_CREATION);
    argsman.AddArg("-datumport=<port>", "Port for embedded Stratum V1 (default: 23334)", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::BLOCK_CREATION);
    argsman.AddArg("-datumupnp", "Use UPnP to map the DATUM Stratum port, with PCP/NAT-PMP fallback (default: 0)", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::BLOCK_CREATION);
    argsman.AddArg("-datumauth", "Require shared Stratum authentication (default: 0)", ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    argsman.AddArg("-datumuser=<user>", "Stratum username; user.worker suffixes are accepted", ArgsManager::ALLOW_ANY | ArgsManager::SENSITIVE, OptionsCategory::BLOCK_CREATION);
    argsman.AddArg("-datumpassword=<password>", "Stratum shared password", ArgsManager::ALLOW_ANY | ArgsManager::SENSITIVE, OptionsCategory::BLOCK_CREATION);
    argsman.AddArg("-datummaxclients=<n>", "Maximum embedded Stratum clients (default: 32)", ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    argsman.AddArg("-datummaxperip=<n>", "Maximum embedded Stratum clients per IP (default: 4)", ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    argsman.AddArg("-datumaddress=<address>", "Fixed operator-controlled Purity payout address", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::BLOCK_CREATION);
    argsman.AddArg("-datumdiff=<n>", "Fixed Stratum share difficulty (default: 65536)", ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    argsman.AddArg("-datumcoinbasetag=<tag>", "Embedded DATUM coinbase tag (default: Bitcoin Purity)", ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    argsman.AddArg("-datumrpcuser=<user>", "Username for embedded DATUM localhost RPC", ArgsManager::ALLOW_ANY | ArgsManager::SENSITIVE, OptionsCategory::RPC);
    argsman.AddArg("-datumrpcpassword=<password>", "Password for embedded DATUM localhost RPC", ArgsManager::ALLOW_ANY | ArgsManager::SENSITIVE, OptionsCategory::RPC);
    argsman.AddArg("-datumrpcurl=<url>", "Advanced loopback HTTP RPC URL override", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::RPC);
}

bool ValidateDatumOptions(const ArgsManager& args, bilingual_str& error)
{
    if (!args.GetBoolArg("-datum", false)) return true;
    if (!args.GetBoolArg("-server", false)) {
        error = Untranslated("-datum=1 requires -server=1 for localhost GBT/submitblock RPC");
        return false;
    }
    const int64_t port{args.GetIntArg("-datumport", DEFAULT_DATUM_PORT)};
    const int64_t max_clients{args.GetIntArg("-datummaxclients", DEFAULT_DATUM_MAX_CLIENTS)};
    const int64_t max_per_ip{args.GetIntArg("-datummaxperip", DEFAULT_DATUM_MAX_PER_IP)};
    const int64_t difficulty{args.GetIntArg("-datumdiff", DEFAULT_DATUM_DIFFICULTY)};
    if (port < 1 || port > 65535) error = Untranslated("-datumport must be between 1 and 65535");
    else if (max_clients < 1 || max_clients > 4096) error = Untranslated("-datummaxclients must be between 1 and 4096");
    else if (max_per_ip < 1 || max_per_ip > max_clients) error = Untranslated("-datummaxperip must be positive and no greater than -datummaxclients");
    else if (difficulty < 1 || difficulty > 2147483647) error = Untranslated("-datumdiff must be between 1 and 2147483647");
    else if (!LookupHost(args.GetArg("-datumlisten", "127.0.0.1"), /*fAllowLookup=*/false)) error = Untranslated("-datumlisten must be a numeric IPv4 or IPv6 address");
    if (!error.empty()) return false;

    const bool datum_upnp{args.GetBoolArg("-datumupnp", DEFAULT_DATUM_UPNP)};
#ifndef USE_UPNP
    if (datum_upnp) {
        error = Untranslated("-datumupnp=1 requires a build with UPnP support");
        return false;
    }
#else
    if (datum_upnp) {
        const auto listen_addr{LookupHost(args.GetArg("-datumlisten", "127.0.0.1"), /*fAllowLookup=*/false)};
        if (!listen_addr || !listen_addr->IsIPv4() || (listen_addr->IsLocal() && !listen_addr->IsBindAny())) {
            error = Untranslated("-datumupnp=1 requires a non-loopback IPv4 -datumlisten (0.0.0.0 is allowed)");
            return false;
        }
    }
#endif // USE_UPNP

    if (args.GetBoolArg("-datumauth", DEFAULT_DATUM_AUTH) &&
        (args.GetArg("-datumuser", "").empty() || args.GetArg("-datumpassword", "").empty())) {
        error = Untranslated("-datum=1 with authentication requires -datumuser and -datumpassword");
        return false;
    }
    const std::string datum_user{args.GetArg("-datumuser", "")};
    if (datum_user.size() > 191 || std::any_of(datum_user.begin(), datum_user.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; })) {
        error = Untranslated("-datumuser must be at most 191 bytes and contain no control characters");
        return false;
    }
    const std::string address{args.GetArg("-datumaddress", "")};
    if (address.empty() || !IsValidDestinationString(address, Params())) {
        error = Untranslated("-datumaddress is missing or invalid for the active Purity network");
        return false;
    }
    if (args.GetArg("-datumcoinbasetag", "Bitcoin Purity").size() > 63) {
        error = Untranslated("-datumcoinbasetag must be at most 63 bytes");
        return false;
    }
    if (args.IsArgSet("-datumrpcurl") && !IsLoopbackRpcUrl(args.GetArg("-datumrpcurl", ""))) {
        error = Untranslated("-datumrpcurl must be a loopback HTTP URL without embedded credentials");
        return false;
    }
    const bool datum_user_set{args.IsArgSet("-datumrpcuser")};
    const bool datum_password_set{args.IsArgSet("-datumrpcpassword")};
    if (datum_user_set != datum_password_set) {
        error = Untranslated("-datumrpcuser and -datumrpcpassword must be configured together");
        return false;
    }
    return true;
}

bool StartDatum(node::NodeContext& node, bilingual_str& error)
{
    const ArgsManager& args{*Assert(node.args)};
    if (!args.GetBoolArg("-datum", false)) return true;

    std::string rpc_user;
    std::string rpc_password;
    if (!GetCredentialPair(args, rpc_user, rpc_password, error)) return false;

    const std::string address{args.GetArg("-datumaddress", "")};
    const CScript payout_script{GetScriptForDestination(DecodeDestination(address))};
    if (payout_script.empty() || payout_script.size() > 64) {
        error = Untranslated("-datumaddress produced an unsupported payout script");
        return false;
    }

    const int64_t rpc_port{args.GetIntArg("-rpcport", BaseParams().RPCPort())};
    const std::string rpc_url{args.GetArg("-datumrpcurl", strprintf("http://127.0.0.1:%d", rpc_port))};
    const std::string listen{args.GetArg("-datumlisten", "127.0.0.1")};
    const std::string auth_user{args.GetArg("-datumuser", "")};
    const std::string auth_password{args.GetArg("-datumpassword", "")};
    const std::string coinbase_tag{args.GetArg("-datumcoinbasetag", "Bitcoin Purity")};
    const bool datum_upnp{args.GetBoolArg("-datumupnp", DEFAULT_DATUM_UPNP)};

    datum_embedded_config config{
        .listen_address = listen.c_str(),
        .listen_port = static_cast<uint16_t>(args.GetIntArg("-datumport", DEFAULT_DATUM_PORT)),
        .max_clients = static_cast<uint32_t>(args.GetIntArg("-datummaxclients", DEFAULT_DATUM_MAX_CLIENTS)),
        .max_clients_per_ip = static_cast<uint32_t>(args.GetIntArg("-datummaxperip", DEFAULT_DATUM_MAX_PER_IP)),
        .auth_required = args.GetBoolArg("-datumauth", DEFAULT_DATUM_AUTH),
        .auth_user = auth_user.c_str(),
        .auth_password = auth_password.c_str(),
        .auth_timeout_seconds = DEFAULT_DATUM_AUTH_TIMEOUT,
        .rpc_url = rpc_url.c_str(),
        .rpc_user = rpc_user.c_str(),
        .rpc_password = rpc_password.c_str(),
        .rpc_cookie_file = "",
        .payout_script = payout_script.data(),
        .payout_script_len = payout_script.size(),
        .payout_address = address.c_str(),
        .share_difficulty = static_cast<uint64_t>(args.GetIntArg("-datumdiff", DEFAULT_DATUM_DIFFICULTY)),
        .coinbase_tag = coinbase_tag.c_str(),
    };
    std::array<char, 512> c_error{};
    g_datum_state.node = &node;
    if (datum_embedded_start(&config, c_error.data(), c_error.size()) != 0) {
        g_datum_state.node = nullptr;
        error = Untranslated(strprintf("Unable to start embedded DATUM: %s", c_error.data()));
        return false;
    }

    g_datum_state.enabled = true;
    g_datum_state.listen = listen;
    g_datum_state.port = config.listen_port;
    g_datum_state.share_difficulty = config.share_difficulty;
    g_datum_state.upnp = datum_upnp;
    g_datum_validation = std::make_unique<DatumValidationInterface>();
    Assert(node.validation_signals)->RegisterValidationInterface(g_datum_validation.get());
    if (datum_upnp && !config.auth_required) {
        LogWarning("[datum] UPnP mapping is enabled while Stratum authentication is disabled; the DATUM port is public");
    }
    if (datum_upnp) StartDatumUpnp(config.listen_port);
    LogInfo("[datum] subsystem started; Stratum configured on %s:%u", listen, config.listen_port);
    return true;
}

void InterruptDatum()
{
    datum_embedded_interrupt();
#ifdef USE_UPNP
    g_datum_upnp_interrupt();
#endif // USE_UPNP
}

void StopDatum(node::NodeContext* node)
{
    if (!g_datum_state.enabled) return;
    node::NodeContext* context{node ? node : g_datum_state.node};
    if (context && context->validation_signals && g_datum_validation) {
        context->validation_signals->UnregisterValidationInterface(g_datum_validation.get());
    }
    g_datum_validation.reset();
    StopDatumUpnp();
    datum_embedded_stop();
    g_datum_state = {};
    LogInfo("[datum] subsystem stopped");
}

UniValue GetDatumInfo()
{
    datum_embedded_stats stats{};
    datum_embedded_get_stats(&stats);
    UniValue result{UniValue::VOBJ};
    result.pushKV("enabled", g_datum_state.enabled);
    result.pushKV("running", stats.running);
    result.pushKV("listen", g_datum_state.listen);
    result.pushKV("port", g_datum_state.port);
    result.pushKV("upnp", g_datum_state.upnp);
    result.pushKV("clients", stats.clients);
    result.pushKV("authorized_clients", stats.authorized_clients);
    const uint64_t share_difficulty{stats.running ? datum_embedded_get_share_difficulty() : g_datum_state.share_difficulty};
    result.pushKV("share_difficulty", share_difficulty);
    result.pushKV("accepted_shares", stats.accepted_shares);
    result.pushKV("rejected_shares", stats.rejected_shares);
    result.pushKV("current_height", stats.current_height);
    return result;
}

bool SetDatumDifficulty(const int64_t difficulty, std::string& error)
{
    if (difficulty < 1 || difficulty > MAX_DATUM_DIFFICULTY) {
        error = strprintf("share difficulty must be between 1 and %d", MAX_DATUM_DIFFICULTY);
        return false;
    }
    std::array<char, 256> c_error{};
    if (datum_embedded_update_share_difficulty(static_cast<uint64_t>(difficulty), c_error.data(), c_error.size()) != 0) {
        error = c_error.data();
        return false;
    }
    LogInfo("[datum] share difficulty updated at runtime to %d", static_cast<int>(difficulty));
    return true;
}

} // namespace mining

extern "C" void datum_bridge_log(int level, const char* message)
{
    switch (level) {
    case DLOG_LEVEL_FATAL:
    case DLOG_LEVEL_ERROR:
        LogError("[datum] %s", message);
        break;
    case DLOG_LEVEL_WARN:
        LogWarning("[datum] %s", message);
        break;
    default:
        LogInfo("[datum] %s", message);
        break;
    }
}

extern "C" void datum_bridge_fatal_error(const char* message)
{
    LogError("[datum] fatal error: %s", message);
    datum_embedded_request_stop();
    if (mining::g_datum_state.node && mining::g_datum_state.node->shutdown_request) {
        mining::g_datum_state.node->shutdown_request();
    }
}
