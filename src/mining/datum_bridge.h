// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
#ifndef BITCOIN_MINING_DATUM_BRIDGE_H
#define BITCOIN_MINING_DATUM_BRIDGE_H

#include <cstdint>
#include <string>

class ArgsManager;
class UniValue;
struct bilingual_str;

namespace node {
struct NodeContext;
}

namespace mining {

void SetupDatumArgs(ArgsManager& argsman);
bool ValidateDatumOptions(const ArgsManager& args, bilingual_str& error);
bool StartDatum(node::NodeContext& node, bilingual_str& error);
void InterruptDatum();
void StopDatum(node::NodeContext* node = nullptr);
UniValue GetDatumInfo();
bool SetDatumDifficulty(int64_t difficulty, std::string& error);

} // namespace mining

#endif // BITCOIN_MINING_DATUM_BRIDGE_H
