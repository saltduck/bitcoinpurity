// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_RDTSGRANDFATHER_H
#define BITCOIN_RPC_RDTSGRANDFATHER_H

#include <script/interpreter.h>
#include <script/sigcache.h>
#include <validation.h>

#include <cstdint>

namespace rdts_audit {
constexpr unsigned int OldFlags(unsigned int normal_flags, int prev_height, int old_boundary)
{
    return prev_height < old_boundary ? normal_flags & ~REDUCED_DATA_MANDATORY_VERIFY_FLAGS : normal_flags;
}

constexpr unsigned int StrictFlags(unsigned int old_flags)
{
    return old_flags | REDUCED_DATA_MANDATORY_VERIFY_FLAGS;
}

constexpr bool IsCandidate(int prev_height, int fixed_boundary, unsigned int old_flags)
{
    return prev_height >= fixed_boundary && (old_flags & REDUCED_DATA_MANDATORY_VERIFY_FLAGS) != REDUCED_DATA_MANDATORY_VERIFY_FLAGS;
}

enum class Outcome { OLD_PASS_STRICT_PASS, OLD_PASS_STRICT_FAIL, OLD_FAIL_STRICT_FAIL, OLD_FAIL_STRICT_PASS };

constexpr Outcome Classify(bool old_pass, bool strict_pass)
{
    if (old_pass) return strict_pass ? Outcome::OLD_PASS_STRICT_PASS : Outcome::OLD_PASS_STRICT_FAIL;
    return strict_pass ? Outcome::OLD_FAIL_STRICT_PASS : Outcome::OLD_FAIL_STRICT_FAIL;
}

constexpr bool IsComplete(uint64_t missing_blocks, uint64_t missing_undo, uint64_t invalid_undo, bool interrupted)
{
    return missing_blocks == 0 && missing_undo == 0 && invalid_undo == 0 && !interrupted;
}

struct Comparison {
    std::optional<std::pair<ScriptError, std::string>> old_error;
    std::optional<std::pair<ScriptError, std::string>> strict_error;

    Outcome GetOutcome() const { return Classify(!old_error, !strict_error); }
};

inline Comparison CompareInput(const CTxOut& spent_output, const CTransaction& tx, SignatureCache& cache, unsigned int vin, unsigned int old_flags, PrecomputedTransactionData& txdata)
{
    CScriptCheck old_check{spent_output, tx, cache, vin, old_flags, false, &txdata};
    CScriptCheck strict_check{spent_output, tx, cache, vin, StrictFlags(old_flags), false, &txdata};
    return {old_check(), strict_check()};
}
} // namespace rdts_audit

#endif // BITCOIN_RPC_RDTSGRANDFATHER_H
