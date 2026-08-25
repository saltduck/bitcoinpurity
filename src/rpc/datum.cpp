// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
#include <mining/datum_bridge.h>

#include <rpc/server.h>
#include <rpc/util.h>

namespace {
RPCHelpMan getdatuminfo()
{
    return RPCHelpMan{
        "getdatuminfo",
        "Returns non-secret embedded DATUM/Stratum status.\n",
        {},
        RPCResult{RPCResult::Type::OBJ, "", /*optional=*/false, "DATUM status", {
            {RPCResult::Type::BOOL, "enabled", "Whether DATUM is enabled at runtime"},
            {RPCResult::Type::BOOL, "running", "Whether the embedded subsystem is running"},
            {RPCResult::Type::STR, "listen", "Configured Stratum listen address"},
            {RPCResult::Type::NUM, "port", "Configured Stratum listen port"},
            {RPCResult::Type::BOOL, "upnp", "Whether DATUM UPnP mapping is configured"},
            {RPCResult::Type::NUM, "clients", "Connected Stratum clients"},
            {RPCResult::Type::NUM, "authorized_clients", "Authorized Stratum clients"},
            {RPCResult::Type::NUM, "share_difficulty", "Fixed Stratum share difficulty"},
            {RPCResult::Type::NUM, "accepted_shares", "Accepted shares for currently connected clients"},
            {RPCResult::Type::NUM, "rejected_shares", "Rejected shares for currently connected clients"},
            {RPCResult::Type::NUM, "current_height", "Current mining job height, or zero"},
        }},
        RPCExamples{HelpExampleCli("getdatuminfo", "")},
        [&](const RPCHelpMan&, const JSONRPCRequest&) -> UniValue {
            return mining::GetDatumInfo();
        },
    };
}

RPCHelpMan setdatumdiff()
{
    return RPCHelpMan{
        "setdatumdiff",
        "Updates the embedded DATUM Stratum share difficulty without restarting. The setting is runtime-only; restart uses -datumdiff again.\n",
        {
            {"difficulty", RPCArg::Type::NUM, RPCArg::Optional::NO, "Fixed Stratum share difficulty (1 to 2147483647)."},
        },
        RPCResult{RPCResult::Type::OBJ, "", /*optional=*/false, "Applied DATUM share difficulty", {
            {RPCResult::Type::NUM, "difficulty", "The new runtime share difficulty"},
        }},
        RPCExamples{HelpExampleCli("setdatumdiff", "1024")},
        [&](const RPCHelpMan&, const JSONRPCRequest& request) -> UniValue {
            const int64_t difficulty{request.params[0].getInt<int64_t>()};
            std::string error;
            if (!mining::SetDatumDifficulty(difficulty, error)) {
                throw JSONRPCError(RPC_MISC_ERROR, error);
            }
            UniValue result{UniValue::VOBJ};
            result.pushKV("difficulty", difficulty);
            return result;
        },
    };
}
} // namespace

void RegisterDatumRPCCommands(CRPCTable& table)
{
    static const CRPCCommand commands[]{
        {"mining", &getdatuminfo},
        {"mining", &setdatumdiff},
    };
    for (const auto& command : commands) table.appendCommand(command.name, &command);
}
