// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_INTERFACES_MINING_H
#define BITCOIN_INTERFACES_MINING_H

#include <consensus/amount.h>       // for CAmount
#include <interfaces/types.h>       // for BlockRef
#include <node/types.h>             // for BlockCreateOptions
#include <primitives/block.h>       // for CBlock, CBlockHeader
#include <primitives/transaction.h> // for CTransactionRef
#include <stdint.h>                 // for int64_t
#include <uint256.h>                // for uint256
#include <util/time.h>              // for MillisecondsDouble

#include <atomic>
#include <memory>   // for unique_ptr, shared_ptr
#include <optional> // for optional
#include <string>
#include <vector>   // for vector

namespace node {
struct CBlockTemplate;
struct NodeContext;
} // namespace node

class BlockValidationState;
class CScript;

namespace interfaces {

//! Block template interface
class BlockTemplate
{
public:
    virtual ~BlockTemplate() = default;

    virtual CBlockHeader getBlockHeader() = 0;
    virtual CBlock getBlock() = 0;

    virtual std::vector<CAmount> getTxFees() = 0;
    virtual std::vector<int64_t> getTxSigops() = 0;
    virtual std::vector<double> getTxCoinAgePriorities() = 0;

    virtual CTransactionRef getCoinbaseTx() = 0;
    //! Structured coinbase fields for in-process clients; existing IPC methods retain their numbering.
    node::CoinbaseTx getCoinbase();
    virtual std::vector<unsigned char> getCoinbaseCommitment() = 0;
    virtual int getWitnessCommitmentIndex() = 0;

    /**
     * Compute merkle path to the coinbase transaction
     *
     * @return merkle path ordered from the deepest
     */
    virtual std::vector<uint256> getCoinbaseMerklePath() = 0;

    /**
     * Construct a block from a copy of this template and submit it for processing.
     *
     * The copy's coinbase is replaced with @p coinbase. @p version, @p timestamp,
     * and @p nonce update the corresponding header fields, and the merkle root is
     * recalculated from the copy. Cached validation results on that copy are
     * discarded. The stored template is left unchanged, so another solution can
     * be submitted from it.
     *
     * @param[in] version   block header version
     * @param[in] timestamp block header time, as a unix timestamp
     * @param[in] nonce     block header nonce
     * @param[in] coinbase  complete coinbase transaction, including its witness
     *
     * @note Unlike the submitblock RPC, this method does not add a missing
     *       coinbase witness. The caller must supply the witness reserved value
     *       that the coinbase commitment commits to.
     *
     * @returns whether the block was processed. That does not necessarily mean
     *          the block is valid or that it became the active chain tip.
     *          Returns true when the block is already known, which can happen
     *          if several nodes construct and broadcast the same solution.
     */
    virtual bool submitSolution(uint32_t version, uint32_t timestamp, uint32_t nonce, CTransactionRef coinbase) = 0;
};

//! Interface giving clients (RPC, Stratum v2 Template Provider in the future)
//! ability to create block templates.
class Mining
{
public:
    virtual ~Mining() = default;

    //! If this chain is exclusively used for testing
    virtual bool isTestChain() = 0;

    //! Returns whether IBD is still in progress.
    virtual bool isInitialBlockDownload() = 0;

    //! Returns the hash and height for the tip of this chain
    virtual std::optional<BlockRef> getTip() = 0;

    /**
     * Waits for the connected tip to change. During node initialization, this will
     * wait until the tip is connected.
     *
     * @param[in] current_tip block hash of the current chain tip. Function waits
     *                        for the chain tip to differ from this.
     * @param[in] timeout     how long to wait for a new tip
     * @returns               Hash and height of the current chain tip after this call.
     */
    virtual std::optional<BlockRef> waitTipChanged(uint256 current_tip, MillisecondsDouble timeout = MillisecondsDouble::max()) = 0;

   /**
     * Construct a new block template. For the createNewBlock variant, subclass options (if any) are silently lost and overridden by any config args. For createNewBlock2, the options are assumed to be complete.
     *
     * @param[in] options options for creating the block
     * @returns a block template
     */
    virtual std::unique_ptr<BlockTemplate> createNewBlock(const node::BlockCreateOptions& options = {}) = 0;
    virtual std::unique_ptr<BlockTemplate> createNewBlock2(const node::BlockCreateOptions& assemble_options) = 0;

    //! In-process mempool lookup. Results preserve input order, with nullptr for missing entries.
    std::vector<CTransactionRef> getTransactionsByTxID(const std::vector<Txid>& txids);
    std::vector<CTransactionRef> getTransactionsByWitnessID(const std::vector<Wtxid>& wtxids);

    //! In-process submission of a complete block, including coinbase witness.
    //! Returns true only for a new accepted block, with reason/debug initialized on every call.
    bool submitBlock(const CBlock& block, std::string& reason, std::string& debug);

    //! Permanently interrupt this in-process client's waits without stopping other clients.
    void interrupt();

    //! Get internal node context. Useful for RPC and testing,
    //! but not accessible across processes.
    virtual node::NodeContext* context() { return nullptr; }

protected:
    std::atomic<bool> m_interrupted{false};
};

//! Return implementation of Mining interface.
std::unique_ptr<Mining> MakeMining(node::NodeContext& node);

//! Wrap an in-process block template in the mining interface.
std::unique_ptr<BlockTemplate> MakeBlockTemplate(std::shared_ptr<node::CBlockTemplate> block_template, node::NodeContext& node);

} // namespace interfaces

#endif // BITCOIN_INTERFACES_MINING_H
