// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
#include <mining/sv2_bridge.h>
#include <rpc/server.h>
#include <rpc/util.h>
namespace {
RPCHelpMan getsv2info()
{
    return RPCHelpMan{"getsv2info","Returns non-secret embedded Stratum V2 gateway status.\n",{},
        RPCResult{RPCResult::Type::OBJ_DYN,"","Aggregate SV2 state",{
            {RPCResult::Type::ANY,"","Lifecycle, template, share and block submission statistics"},
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
