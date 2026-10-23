// Copyright (c) 2019-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <signet.h>

#include <common/system.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <hash.h>
#include <logging.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <span.h>
#include <streams.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

static constexpr uint8_t SIGNET_HEADER[4] = {0xec, 0xc7, 0xda, 0xa2};

std::string SignetConsensusFingerprint(const Consensus::Params& params)
{
    if (params.signet_preparing) return "preparing";
    HashWriter writer;
    writer << params.nPurityActivationHeight << params.hashSignetForkParent
           << params.nAsertAnchorHeight << params.hashSignetAsertAnchor
           << params.hashSignetAsertAnchorParent << params.nSignetAsertAnchorBits
           << params.nSignetAsertAnchorParentTime << params.signet_challenge_postfork;
    // The first-block pin may be filled after mining without changing these rules.
    return writer.GetHash().GetHex();
}

static constexpr unsigned int BLOCK_SCRIPT_VERIFY_FLAGS = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_NULLDUMMY;

static bool FetchAndClearCommitmentSection(const Span<const uint8_t> header, CScript& witness_commitment, std::vector<uint8_t>& result)
{
    CScript replacement;
    bool found_header = false;
    result.clear();

    opcodetype opcode;
    CScript::const_iterator pc = witness_commitment.begin();
    std::vector<uint8_t> pushdata;
    while (witness_commitment.GetOp(pc, opcode, pushdata)) {
        if (pushdata.size() > 0) {
            if (!found_header && pushdata.size() > header.size() && std::ranges::equal(Span{pushdata}.first(header.size()), header)) {
                // pushdata only counts if it has the header _and_ some data
                result.insert(result.end(), pushdata.begin() + header.size(), pushdata.end());
                pushdata.erase(pushdata.begin() + header.size(), pushdata.end());
                found_header = true;
            }
            replacement << pushdata;
        } else {
            replacement << opcode;
        }
    }

    if (found_header) witness_commitment = replacement;
    return found_header;
}

static uint256 ComputeModifiedMerkleRoot(const CMutableTransaction& cb, const CBlock& block)
{
    std::vector<uint256> leaves;
    leaves.resize(block.vtx.size());
    leaves[0] = cb.GetHash();
    for (size_t s = 1; s < block.vtx.size(); ++s) {
        leaves[s] = block.vtx[s]->GetHash();
    }
    return ComputeMerkleRoot(std::move(leaves));
}

std::optional<SignetTxs> SignetTxs::Create(const CBlock& block, const CScript& challenge)
{
    CMutableTransaction tx_to_spend;
    tx_to_spend.version = 0;
    tx_to_spend.nLockTime = 0;
    tx_to_spend.vin.emplace_back(COutPoint(), CScript(OP_0), 0);
    tx_to_spend.vout.emplace_back(0, challenge);

    CMutableTransaction tx_spending;
    tx_spending.version = 0;
    tx_spending.nLockTime = 0;
    tx_spending.vin.emplace_back(COutPoint(), CScript(), 0);
    tx_spending.vout.emplace_back(0, CScript(OP_RETURN));

    // can't fill any other fields before extracting signet
    // responses from block coinbase tx

    // find and delete signet signature
    if (block.vtx.empty()) return std::nullopt; // no coinbase tx in block; invalid
    CMutableTransaction modified_cb(*block.vtx.at(0));

    const int cidx = GetWitnessCommitmentIndex(block);
    if (cidx == NO_WITNESS_COMMITMENT) {
        return std::nullopt; // require a witness commitment
    }

    CScript& witness_commitment = modified_cb.vout.at(cidx).scriptPubKey;

    std::vector<uint8_t> signet_solution;
    if (!FetchAndClearCommitmentSection(SIGNET_HEADER, witness_commitment, signet_solution)) {
        // no signet solution -- allow this to support OP_TRUE as trivial block challenge
    } else {
        try {
            SpanReader v{signet_solution};
            v >> tx_spending.vin[0].scriptSig;
            v >> tx_spending.vin[0].scriptWitness.stack;
            if (!v.empty()) return std::nullopt; // extraneous data encountered
        } catch (const std::exception&) {
            return std::nullopt; // parsing error
        }
    }
    uint256 signet_merkle = ComputeModifiedMerkleRoot(modified_cb, block);

    std::vector<uint8_t> block_data;
    VectorWriter writer{block_data, 0};
    writer << block.nVersion;
    writer << block.hashPrevBlock;
    writer << signet_merkle;
    writer << block.nTime;
    tx_to_spend.vin[0].scriptSig << block_data;
    tx_spending.vin[0].prevout = COutPoint(tx_to_spend.GetHash(), 0);

    return SignetTxs{tx_to_spend, tx_spending};
}

// Signet block solution checker
const std::vector<uint8_t>& GetSignetChallengeForHeight(const Consensus::Params& params, int height)
{
    return height >= params.nPurityActivationHeight ? params.signet_challenge_postfork : params.signet_challenge;
}

bool CheckSignetBlockSolution(const CBlock& block, const Consensus::Params& consensusParams, int height)
{
    if (height < 0) return false;
    if (height == 0) return block.GetHash() == consensusParams.hashGenesisBlock;

    const auto& challenge_bytes = GetSignetChallengeForHeight(consensusParams, height);
    if (challenge_bytes.empty()) return false;
    if (height >= consensusParams.nPurityActivationHeight) {
        const int index = GetWitnessCommitmentIndex(block);
        if (index == NO_WITNESS_COMMITMENT) return false;
        const CScript& commitment = block.vtx[0]->vout[index].scriptPubKey;
        if (commitment.size() > 195) return false;
        auto pc = commitment.begin() + MINIMUM_WITNESS_COMMITMENT;
        std::vector<unsigned char> data;
        opcodetype op;
        if (!commitment.GetOp(pc, op, data) || pc != commitment.end() || data.size() <= 4 ||
            !std::ranges::equal(Span{data}.first(4), SIGNET_HEADER) ||
            CScript(commitment.begin() + MINIMUM_WITNESS_COMMITMENT, commitment.end()) != (CScript() << data)) {
            return false;
        }
        try {
            SpanReader reader{Span{data}.subspan(4)};
            const uint64_t size = ReadCompactSize(reader);
            if (size > 149) return false;
            CScript script_sig;
            script_sig.resize(size);
            reader.read(MakeWritableByteSpan(script_sig));
            if (ReadCompactSize(reader) != 0 || !reader.empty()) return false;
            CScript::const_iterator it = script_sig.begin();
            std::vector<unsigned char> signature;
            if (!script_sig.GetOp(it, op, signature) || op != OP_0) return false;
            for (int signer = 0; signer < 2; ++signer) {
                if (!script_sig.GetOp(it, op, signature) || signature.empty() || signature.size() > 73 ||
                    static_cast<size_t>(op) != signature.size()) return false;
            }
            if (it != script_sig.end()) return false;
        } catch (const std::exception&) {
            return false;
        }
    }

    const CScript challenge(challenge_bytes.begin(), challenge_bytes.end());
    const std::optional<SignetTxs> signet_txs = SignetTxs::Create(block, challenge);

    if (!signet_txs) {
        LogDebug(BCLog::VALIDATION, "CheckSignetBlockSolution: Errors in block (block solution parse failure)\n");
        return false;
    }

    const CScript& scriptSig = signet_txs->m_to_sign.vin[0].scriptSig;
    const CScriptWitness& witness = signet_txs->m_to_sign.vin[0].scriptWitness;

    PrecomputedTransactionData txdata;
    txdata.Init(signet_txs->m_to_sign, {signet_txs->m_to_spend.vout[0]});
    TransactionSignatureChecker sigcheck(&signet_txs->m_to_sign, /* nInIn= */ 0, /* amountIn= */ signet_txs->m_to_spend.vout[0].nValue, txdata, MissingDataBehavior::ASSERT_FAIL);

    if (!VerifyScript(scriptSig, signet_txs->m_to_spend.vout[0].scriptPubKey, &witness, BLOCK_SCRIPT_VERIFY_FLAGS, sigcheck)) {
        LogDebug(BCLog::VALIDATION, "CheckSignetBlockSolution: Errors in block (block solution invalid)\n");
        return false;
    }
    return true;
}

bool CheckSignetCoinbaseOutputSizes(const CBlock& block, const Consensus::Params& params, int height, TxValidationState& state)
{
    if (block.vtx.empty()) return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-cb-missing");
    if (!params.signet_blocks || height < params.nPurityActivationHeight) {
        return Consensus::CheckOutputSizes(*block.vtx[0], state);
    }
    if (!block.vtx[0]->IsCoinBase() || !CheckSignetBlockSolution(block, params, height)) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-signet-blksig");
    }
    CMutableTransaction other_outputs{*block.vtx[0]};
    other_outputs.vout.erase(other_outputs.vout.begin() + GetWitnessCommitmentIndex(block));
    return Consensus::CheckOutputSizes(CTransaction{other_outputs}, state);
}
