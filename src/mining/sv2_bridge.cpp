// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
#include <mining/sv2_bridge.h>
#include <mining/sv2_template.h>
#include <common/args.h>
#include <logging.h>
#include <node/context.h>
#include <univalue.h>
#include <util/translation.h>
#include <array>
#include <memory>
#include <mutex>
#include <string>
namespace mining {
namespace {
std::mutex g_mutex;
std::unique_ptr<Sv2TemplateProvider> g_provider;
void* g_handle{nullptr};
UniValue Config(const ArgsManager& args)
{
    UniValue config{UniValue::VOBJ};
    const auto listen=args.GetArg("-sv2listen","127.0.0.1");
    config.pushKV("listen",(listen.find(':')==std::string::npos ? listen : "["+listen+"]")+":"+std::to_string(args.GetIntArg("-sv2port",23334)));
    for (const auto& [key,option]:{std::pair{"pool","-sv2pool"},{"jds","-sv2jds"},{"pool_key","-sv2poolkey"},{"jds_key","-sv2jdskey"},{"identity","-sv2identity"},{"user","-sv2user"},{"password","-sv2password"}}) config.pushKV(key,args.GetArg(option,""));
    config.pushKV("auth",args.GetBoolArg("-sv2auth",false));
    config.pushKV("max_clients",args.GetIntArg("-sv2maxclients",32));
    config.pushKV("max_per_ip",args.GetIntArg("-sv2maxperip",4));
    return config;
}
}
void SetupSv2Args(ArgsManager& args)
{
    args.AddArg("-sv2","Enable embedded SV2 pooled mining (default: 0)",ArgsManager::ALLOW_ANY,OptionsCategory::OPTIONS);
    args.AddArg("-sv2listen=<addr>","Numeric IPv4/IPv6 miner listen address (default: 127.0.0.1)",ArgsManager::ALLOW_ANY,OptionsCategory::OPTIONS);
    args.AddArg("-sv2port=<port>","V1 miner port (default: 23334)",ArgsManager::ALLOW_ANY,OptionsCategory::OPTIONS);
    args.AddArg("-sv2pool=<host:port>","SV2 Mining endpoint",ArgsManager::ALLOW_ANY,OptionsCategory::OPTIONS);
    args.AddArg("-sv2jds=<host:port>","SV2 Full-Template Job Declaration endpoint",ArgsManager::ALLOW_ANY,OptionsCategory::OPTIONS);
    args.AddArg("-sv2poolkey=<key>","Mining authority public key (32-byte hex or SRI Base58)",ArgsManager::ALLOW_ANY,OptionsCategory::OPTIONS);
    args.AddArg("-sv2jdskey=<key>","JDS authority public key (32-byte hex or SRI Base58)",ArgsManager::ALLOW_ANY,OptionsCategory::OPTIONS);
    args.AddArg("-sv2identity=<account>","Pool account identity for this gateway",ArgsManager::ALLOW_ANY|ArgsManager::SENSITIVE,OptionsCategory::OPTIONS);
    args.AddArg("-sv2auth","Require local miner authentication (default: 0)",ArgsManager::ALLOW_ANY,OptionsCategory::OPTIONS);
    args.AddArg("-sv2user=<user>","Local miner username; user.worker is also accepted",ArgsManager::ALLOW_ANY|ArgsManager::SENSITIVE,OptionsCategory::OPTIONS);
    args.AddArg("-sv2password=<password>","Local miner password",ArgsManager::ALLOW_ANY|ArgsManager::SENSITIVE,OptionsCategory::OPTIONS);
    args.AddArg("-sv2maxclients=<n>","Maximum miner connections (default: 32, maximum: 4096)",ArgsManager::ALLOW_ANY,OptionsCategory::OPTIONS);
    args.AddArg("-sv2maxperip=<n>","Maximum miners per IP (default: 4)",ArgsManager::ALLOW_ANY,OptionsCategory::OPTIONS);
}
bool ValidateSv2Options(const ArgsManager& args,bilingual_str& error)
{
    if (!args.GetBoolArg("-sv2",false)) return true;
    if (args.GetBoolArg("-datum",false)) {error=Untranslated("DATUM Solo and SV2 pooled mining cannot be enabled together");return false;}
    if (args.GetChainType()!=ChainType::MAIN && args.GetChainType()!=ChainType::REGTEST) {error=Untranslated("SV2 supports only mainnet and regtest");return false;}
    const auto port=args.GetIntArg("-sv2port",23334);
    if (port<1 || port>65535) {error=Untranslated("-sv2port must be between 1 and 65535");return false;}
    const auto config=Config(args).write();
    if (purity_sv2_validate(reinterpret_cast<const uint8_t*>(config.data()),config.size())!=0) {
        error=Untranslated("Invalid SV2 configuration: check endpoints, authority keys, identity, authentication and client limits");return false;
    }
    return true;
}
bool StartSv2(node::NodeContext& node,bilingual_str& error)
{
    if (!node.args->GetBoolArg("-sv2",false)) return true;
    std::lock_guard lock{g_mutex};
    g_provider=std::make_unique<Sv2TemplateProvider>(node);
    const auto config=Config(*node.args).write();
    g_handle=purity_sv2_start(reinterpret_cast<const uint8_t*>(config.data()),config.size(),g_provider->Callbacks());
    if (!g_handle) {g_provider.reset();error=Untranslated("Unable to initialize SV2 gateway or bind its miner listener");return false;}
    LogInfo("[sv2] embedded gateway started");
    return true;
}
void InterruptSv2()
{
    std::lock_guard lock{g_mutex};
    if (g_provider) g_provider->Interrupt();
    if (g_handle) purity_sv2_interrupt(g_handle);
}
void StopSv2()
{
    std::lock_guard lock{g_mutex};
    if (g_handle) {purity_sv2_stop(g_handle);g_handle=nullptr;}
    g_provider.reset();
}
UniValue GetSv2Info()
{
    std::lock_guard lock{g_mutex};
    UniValue result{UniValue::VOBJ};
    if (!g_handle) {
        result.pushKV("enabled",false);result.pushKV("running",false);result.pushKV("status","disabled");return result;
    }
    std::array<uint8_t,8192> data{};
    const size_t size=purity_sv2_status(g_handle,data.data(),data.size());
    if (size==0 || size>data.size() || !result.read(std::string{reinterpret_cast<const char*>(data.data()),size})) {
        result=UniValue{UniValue::VOBJ};result.pushKV("enabled",true);result.pushKV("running",false);result.pushKV("status","status-unavailable");
    }
    return result;
}
}
