// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
#ifndef BITCOIN_MINING_SV2_BRIDGE_H
#define BITCOIN_MINING_SV2_BRIDGE_H
class ArgsManager;
class UniValue;
struct bilingual_str;
namespace node {struct NodeContext;}
namespace mining {
void SetupSv2Args(ArgsManager&);
bool ValidateSv2Options(const ArgsManager&,bilingual_str&);
bool StartSv2(node::NodeContext&,bilingual_str&);
void InterruptSv2();
void StopSv2();
UniValue GetSv2Info();
}
#endif // BITCOIN_MINING_SV2_BRIDGE_H
