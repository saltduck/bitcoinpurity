// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>

#include <chainparamsbase.h>
#include <common/args.h>
#include <consensus/params.h>
#include <deploymentinfo.h>
#include <kernel/chainparams.h>
#include <kernel/official_packages.h>
#include <netbase.h>
#include <arith_uint256.h>
#include <protocol.h>
#include <pubkey.h>
#include <script/script.h>
#include <streams.h>
#include <univalue.h>
#include <logging.h>
#include <tinyformat.h>
#include <util/chaintype.h>
#include <util/strencodings.h>
#include <util/string.h>

#include <cassert>
#include <cstdint>
#include <limits>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using util::SplitString;

void ReadSigNetArgs(const ArgsManager& args, CChainParams::SigNetOptions& options)
{
    if (args.IsArgSet("-signetchallenge") || args.IsArgSet("-signetblocktime")) {
        throw std::runtime_error("Signet consensus is configured by -signetparams; -signetchallenge and -signetblocktime are not supported");
    }
    if (!args.GetArgs("-signetseednode").empty()) options.seeds = args.GetArgs("-signetseednode");
    const fs::path path = AbsPathForConfigVal(args, args.GetPathArg("-signetparams", "signet-params.json"), false);
    if (!args.IsArgSet("-signetparams") && !fs::exists(path)) return;
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("Cannot read Signet parameter configuration");
    const std::string contents{std::istreambuf_iterator<char>{input}, {}};
    UniValue json;
    if (!json.read(contents) || !json.isObject() || JsonHasDuplicateKeys(json)) {
        throw std::runtime_error("Signet parameters must be a JSON object without duplicate keys");
    }
    const std::set<std::string> fields{"schema_version", "simulation", "activation_height", "parent_hash", "activation_hash", "anchor", "public_keys", "minimum_chain_work", "p2p_port", "rpc_port", "dns_seeds", "fixed_seed_nodes", "historical_seed_nodes"};
    if (json.getKeys().size() != fields.size()) throw std::runtime_error("Signet parameters must include every schema field");
    for (const auto& key : json.getKeys()) {
        if (!fields.contains(key)) throw std::runtime_error("Unknown Signet parameter field: " + key);
    }
    const int schema = json["schema_version"].getInt<int>();
    if (schema != 1 && schema != 2) throw std::runtime_error("Unsupported Signet parameter schema version");
    CChainParams::SigNetOptions::Fork fork{};
    fork.simulation = json["simulation"].get_bool();
    const auto hash = [](const UniValue& object, const std::string& field) {
        const auto value = object[field].isStr() ? uint256::FromHex(object[field].get_str()) : std::nullopt;
        if (!value || value->IsNull()) throw std::runtime_error("Invalid Signet hash field: " + field);
        return *value;
    };
    fork.preparing = schema == 2 && json["activation_height"].isNull();
    if (fork.preparing) {
        for (const auto* field : {"parent_hash", "anchor", "activation_hash"}) {
            if (!json[field].isNull()) throw std::runtime_error("Preparing Signet activation fields must all be null");
        }
    } else {
        fork.activation_height = json["activation_height"].getInt<int>();
        if (fork.activation_height < 6 || fork.activation_height == std::numeric_limits<int>::max()) {
            throw std::runtime_error("Signet activation_height must be in [6, INT_MAX)");
        }
        fork.parent_hash = hash(json, "parent_hash");
        if (json["activation_hash"].isNull() && fork.simulation) {
            fork.activation_hash.SetNull();
        } else {
            fork.activation_hash = hash(json, "activation_hash");
        }
        const auto& anchor = json["anchor"];
        if (!anchor.isObject() || anchor.getKeys().size() != 5) throw std::runtime_error("Invalid Signet anchor object");
        fork.anchor_height = anchor["height"].getInt<int>();
        if (fork.anchor_height < 1 || fork.anchor_height >= fork.activation_height) throw std::runtime_error("Signet anchor must precede activation and have a parent");
        if (schema == 2 && fork.anchor_height != fork.activation_height - 5) throw std::runtime_error("Signet schema v2 anchor must be H-5");
        fork.anchor_hash = hash(anchor, "hash");
        fork.anchor_parent_hash = hash(anchor, "parent_hash");
        const auto bits = TryParseHex<uint8_t>(anchor["bits"].get_str());
        if (!bits || bits->size() != 4) throw std::runtime_error("Invalid Signet anchor bits");
        fork.anchor_bits = (uint32_t((*bits)[0]) << 24) | (uint32_t((*bits)[1]) << 16) | (uint32_t((*bits)[2]) << 8) | (*bits)[3];
        arith_uint256 target;
        bool negative, overflow;
        target.SetCompact(fork.anchor_bits, &negative, &overflow);
        if (negative || overflow || target == 0 || target > UintToArith256(uint256{"00000377ae000000000000000000000000000000000000000000000000000000"})) {
            throw std::runtime_error("Signet anchor target exceeds the inherited powLimit");
        }
        fork.anchor_parent_time = anchor["parent_time"].getInt<uint32_t>();
        if (fork.anchor_parent_time == 0) throw std::runtime_error("Invalid Signet anchor parent_time");
    }
    const auto& keys = json["public_keys"];
    if (!keys.isArray() || keys.size() != 3) throw std::runtime_error("Signet requires three compressed public keys");
    std::set<std::vector<uint8_t>> unique;
    CScript challenge;
    challenge << OP_2;
    for (const auto& item : keys.getValues()) {
        const auto key = item.isStr() ? TryParseHex<uint8_t>(item.get_str()) : std::nullopt;
        if (!key || key->size() != 33 || !CPubKey{*key}.IsFullyValid() || !unique.insert(*key).second) {
            throw std::runtime_error("Signet public keys must be distinct valid compressed public keys");
        }
        challenge << *key;
    }
    challenge << OP_3 << OP_CHECKMULTISIG;
    fork.challenge.assign(challenge.begin(), challenge.end());
    fork.minimum_chain_work = hash(json, "minimum_chain_work");
    const auto port = [](const UniValue& object, const std::string& field) {
        const int value = object[field].getInt<int>();
        if (value < 1 || value > 65535) throw std::runtime_error("Invalid Signet port field: " + field);
        return static_cast<uint16_t>(value);
    };
    fork.p2p_port = port(json, "p2p_port");
    fork.rpc_port = port(json, "rpc_port");
    if (fork.p2p_port == fork.rpc_port) throw std::runtime_error("Signet P2P and RPC ports must differ");
    const auto list = [](const UniValue& object, const std::string& field) {
        const auto& values = object[field];
        if (!values.isArray()) throw std::runtime_error("Invalid Signet list field: " + field);
        std::vector<std::string> result;
        for (const auto& value : values.getValues()) {
            if (!value.isStr() || value.get_str().empty()) throw std::runtime_error("Invalid Signet seed entry");
            result.push_back(value.get_str());
        }
        return result;
    };
    fork.dns_seeds = list(json, "dns_seeds");
    fork.historical_seed_nodes = list(json, "historical_seed_nodes");
    VectorWriter seeds{fork.fixed_seeds, 0};
    ParamsStream seed_writer{seeds, CAddress::V2_NETWORK};
    for (const auto& seed : list(json, "fixed_seed_nodes")) {
        const CService endpoint = LookupNumeric(seed);
        if (!endpoint.IsValid() || endpoint.GetPort() == 0) throw std::runtime_error("Signet fixed seed must be a numeric address with a port");
        seed_writer << endpoint;
    }
    options.fork = std::move(fork);
}

void ReadRegTestArgs(const ArgsManager& args, CChainParams::RegTestOptions& options)
{
    if (auto value = args.GetBoolArg("-fastprune")) options.fastprune = *value;
    if (HasTestOption(args, "bip94")) options.enforce_bip94 = true;

    for (const std::string& arg : args.GetArgs("-testactivationheight")) {
        const auto found{arg.find('@')};
        if (found == std::string::npos) {
            throw std::runtime_error(strprintf("Invalid format (%s) for -testactivationheight=name@height.", arg));
        }

        const auto value{arg.substr(found + 1)};
        int32_t height;
        if (!ParseInt32(value, &height) || height < 0 || height >= std::numeric_limits<int>::max()) {
            throw std::runtime_error(strprintf("Invalid height value (%s) for -testactivationheight=name@height.", arg));
        }

        const auto deployment_name{arg.substr(0, found)};
        if (deployment_name == "purity") {
            options.purity_activation_height = height;
        } else if (deployment_name == "rdtsgrandfatherfix") {
            options.reduced_data_grandfather_fix_height = height;
        } else if (deployment_name == "strictmultisigutxo") {
            options.strict_multisig_utxo_height = height;
        } else if (deployment_name == "strictmultisig") {
            options.strict_multisig_enforcement_height = height;
        } else if (const auto buried_deployment = GetBuriedDeployment(deployment_name)) {
            options.activation_heights[*buried_deployment] = height;
        } else {
            throw std::runtime_error(strprintf("Invalid name (%s) for -testactivationheight=name@height.", arg));
        }
    }

    if (options.strict_multisig_utxo_height.has_value() != options.strict_multisig_enforcement_height.has_value()) {
        throw std::runtime_error("-testactivationheight requires both strictmultisigutxo and strictmultisig heights.");
    }
    if (options.strict_multisig_utxo_height && *options.strict_multisig_utxo_height >= *options.strict_multisig_enforcement_height) {
        throw std::runtime_error("-testactivationheight=strictmultisigutxo height must be less than the strictmultisig height.");
    }

    for (const std::string& strDeployment : args.GetArgs("-vbparams")) {
        std::vector<std::string> vDeploymentParams = SplitString(strDeployment, ':');
        if (vDeploymentParams.size() < 3 || 7 < vDeploymentParams.size()) {
            throw std::runtime_error("Version bits parameters malformed, expecting deployment:start:end[:min_activation_height[:max_activation_height[:active_duration[:threshold]]]]");
        }
        CChainParams::VersionBitsParameters vbparams{};
        if (!ParseInt64(vDeploymentParams[1], &vbparams.start_time)) {
            throw std::runtime_error(strprintf("Invalid nStartTime (%s)", vDeploymentParams[1]));
        }
        if (!ParseInt64(vDeploymentParams[2], &vbparams.timeout)) {
            throw std::runtime_error(strprintf("Invalid nTimeout (%s)", vDeploymentParams[2]));
        }
        if (vDeploymentParams.size() >= 4) {
            if (!ParseInt32(vDeploymentParams[3], &vbparams.min_activation_height)) {
                throw std::runtime_error(strprintf("Invalid min_activation_height (%s)", vDeploymentParams[3]));
            }
        } else {
            vbparams.min_activation_height = 0;
        }
        if (vDeploymentParams.size() >= 5) {
            if (!ParseInt32(vDeploymentParams[4], &vbparams.max_activation_height)) {
                throw std::runtime_error(strprintf("Invalid max_activation_height (%s)", vDeploymentParams[4]));
            }
        }
        if (vDeploymentParams.size() >= 6) {
            if (!ParseInt32(vDeploymentParams[5], &vbparams.active_duration)) {
                throw std::runtime_error(strprintf("Invalid active_duration (%s)", vDeploymentParams[5]));
            }
        }
        if (vDeploymentParams.size() >= 7) {
            if (!ParseInt32(vDeploymentParams[6], &vbparams.threshold)) {
                throw std::runtime_error(strprintf("Invalid threshold (%s)", vDeploymentParams[6]));
            }
        }
        // Validate that timeout and max_activation_height are mutually exclusive
        if (vbparams.timeout != Consensus::BIP9Deployment::NO_TIMEOUT && vbparams.max_activation_height < std::numeric_limits<int>::max()) {
            throw std::runtime_error(strprintf("Cannot specify both timeout (%ld) and max_activation_height (%d) for deployment %s. Use timeout for BIP9 or max_activation_height for mandatory activation deadline, not both.", vbparams.timeout, vbparams.max_activation_height, vDeploymentParams[0]));
        }
        bool found = false;
        for (int j=0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j) {
            if (vDeploymentParams[0] == VersionBitsDeploymentInfo[j].name) {
                options.version_bits_parameters[Consensus::DeploymentPos(j)] = vbparams;
                found = true;
                LogPrintf("Setting version bits activation parameters for %s to start=%ld, timeout=%ld, min_activation_height=%d, max_activation_height=%d, active_duration=%d, threshold=%d\n", vDeploymentParams[0], vbparams.start_time, vbparams.timeout, vbparams.min_activation_height, vbparams.max_activation_height, vbparams.active_duration, vbparams.threshold);
                break;
            }
        }
        if (!found) {
            throw std::runtime_error(strprintf("Invalid deployment (%s)", vDeploymentParams[0]));
        }
    }
}

static std::unique_ptr<const CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<const CChainParams> CreateChainParams(const ArgsManager& args, const ChainType chain)
{
    g_rdts_consent = RDTSConsentFlag::IMPLICIT;
    g_enable_rdts = true;

    switch (chain) {
    case ChainType::MAIN:
        return CChainParams::Main({});
    case ChainType::TESTNET:
        return CChainParams::TestNet();
    case ChainType::TESTNET4:
        return CChainParams::TestNet4();
    case ChainType::SIGNET: {
        auto opts = CChainParams::SigNetOptions{};
        ReadSigNetArgs(args, opts);
        return CChainParams::SigNet(opts);
    }
    case ChainType::REGTEST: {
        auto opts = CChainParams::RegTestOptions{};
        ReadRegTestArgs(args, opts);
        return CChainParams::RegTest(opts);
    }
    }
    assert(false);
}

void SelectParams(const ChainType chain)
{
    if (chain == ChainType::SIGNET) {
        gArgs.SelectConfigNetwork("signet");
        auto params = CreateChainParams(gArgs, chain);
        SelectBaseParams(chain, params->SignetRPCPort());
        globalChainParams = std::move(params);
        return;
    }
    SelectBaseParams(chain);
    globalChainParams = CreateChainParams(gArgs, chain);
}
