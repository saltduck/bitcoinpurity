// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
#include <mining/sv2_bridge.h>
#include <rpc/server.h>
#include <rpc/util.h>
namespace {
RPCHelpMan getsv2info()
{
    return RPCHelpMan{"getsv2info","Returns non-secret embedded Stratum V2 gateway status.\n",{},
        RPCResult{RPCResult::Type::OBJ,"","Aggregate SV2 state",{
            {RPCResult::Type::BOOL,"enabled","Whether SV2 is enabled"},
            {RPCResult::Type::BOOL,"running","Whether the gateway is running"},
            {RPCResult::Type::STR,"status","Gateway lifecycle state"},
            {RPCResult::Type::STR,"stage",/*optional=*/true,"Current SV2/JDS operation"},
            {RPCResult::Type::STR,"error",/*optional=*/true,"Error"},
            {RPCResult::Type::STR,"listen",/*optional=*/true,"Listen"},
            {RPCResult::Type::NUM,"port",/*optional=*/true,"Port"},
            {RPCResult::Type::BOOL,"auth_required",/*optional=*/true,"Auth required"},
            {RPCResult::Type::NUM,"subscribed_clients",/*optional=*/true,"Subscribed clients"},
            {RPCResult::Type::NUM,"clients",/*optional=*/true,"Clients"},
            {RPCResult::Type::NUM,"authorized_clients",/*optional=*/true,"Authorized clients"},
            {RPCResult::Type::NUM,"local_valid_shares",/*optional=*/true,"Local valid shares"},
            {RPCResult::Type::NUM,"accepted_shares",/*optional=*/true,"Accepted shares"},
            {RPCResult::Type::NUM,"rejected_shares",/*optional=*/true,"Rejected shares"},
            {RPCResult::Type::NUM,"pending_shares",/*optional=*/true,"Pending shares"},
            {RPCResult::Type::NUM,"accepted_difficulty",/*optional=*/true,"Accepted difficulty"},
            {RPCResult::Type::NUM,"best_share_difficulty",/*optional=*/true,"Best share difficulty"},
            {RPCResult::Type::NUM,"session_started",/*optional=*/true,"Session started"},
            {RPCResult::Type::NUM,"last_share_time",/*optional=*/true,"Last share time"},
            {RPCResult::Type::NUM,"current_height",/*optional=*/true,"Current height"},
            {RPCResult::Type::NUM,"abandoned_shares",/*optional=*/true,"Abandoned shares"},
            {RPCResult::Type::STR,"job_id",/*optional=*/true,"Job id"},
            {RPCResult::Type::NUM,"job_created",/*optional=*/true,"Job created"},
            {RPCResult::Type::STR,"previous_block_hash",/*optional=*/true,"Previous block hash"},
            {RPCResult::Type::NUM,"nbits",/*optional=*/true,"Nbits"},
            {RPCResult::Type::NUM,"network_difficulty",/*optional=*/true,"Network difficulty"},
            {RPCResult::Type::NUM,"share_difficulty",/*optional=*/true,"Share difficulty"},
            {RPCResult::Type::NUM,"transactions",/*optional=*/true,"Transactions"},
            {RPCResult::Type::NUM,"template_size",/*optional=*/true,"Template size"},
            {RPCResult::Type::NUM,"template_weight",/*optional=*/true,"Template weight"},
            {RPCResult::Type::NUM,"coinbase_value",/*optional=*/true,"Coinbase value"},
            {RPCResult::Type::NUM,"block_candidates",/*optional=*/true,"Block candidates"},
            {RPCResult::Type::NUM,"blocks_accepted",/*optional=*/true,"Blocks accepted"},
            {RPCResult::Type::NUM,"blocks_rejected",/*optional=*/true,"Blocks rejected"},
            {RPCResult::Type::NUM,"last_block_time",/*optional=*/true,"Last block time"},
            {RPCResult::Type::STR,"last_block_hash",/*optional=*/true,"Last block hash"},
            {RPCResult::Type::STR,"last_block_result",/*optional=*/true,"Last block result"},
        }},
        RPCExamples{HelpExampleCli("getsv2info","")},
        [&](const RPCHelpMan&,const JSONRPCRequest&)->UniValue {return mining::GetSv2Info();}};
}
}
void RegisterSv2RPCCommands(CRPCTable& table)
{
    static const CRPCCommand commands[]={{"mining",&getsv2info}};
    for (const auto& command:commands) table.appendCommand(command.name,&command);
}
