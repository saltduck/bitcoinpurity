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
#include <stdexcept>
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
    virtual std::vector<double> getTxCoinAgePriorities() { throw std::runtime_error("Coin-age priorities are not part of Core v32.1 IPC"); }

    virtual CTransactionRef getCoinbaseTransaction() { return getBlock().vtx.at(0); }
    //! Structured coinbase fields exposed as getCoinbaseTx on the Core IPC wire.
    virtual node::CoinbaseTx getCoinbaseTx();
    node::CoinbaseTx getCoinbase() { return getCoinbaseTx(); }
    virtual std::vector<unsigned char> getCoinbaseCommitment() { throw std::runtime_error("Use structured coinbase outputs on Core v32.1 IPC"); }
    virtual int getWitnessCommitmentIndex() { throw std::runtime_error("Use structured coinbase outputs on Core v32.1 IPC"); }

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
     * @returns true only for a newly accepted block. Duplicate submissions fail.
     * This in-process convenience method retains the original template snapshot.
     * Core submitSolution updates the stored block and reports reason/debug.
     */
    virtual bool submitSolutionLegacy(uint32_t version, uint32_t timestamp, uint32_t nonce, CTransactionRef coinbase)
    {
        std::string reason, debug;
        return submitSolution(version, timestamp, nonce, std::move(coinbase), reason, debug);
    }
    virtual bool submitSolution(uint32_t version, uint32_t timestamp, uint32_t nonce, CTransactionRef coinbase, std::string& reason, std::string& debug) = 0;
    virtual bool submitSolutionOld7(uint32_t, uint32_t, uint32_t, CTransactionRef)
    {
        throw std::runtime_error("Old submitSolution (@7) not supported. Please update your client!");
    }
    virtual std::unique_ptr<BlockTemplate> waitNext(node::BlockWaitOptions options = {}) = 0;
    virtual void interruptWait() = 0;
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
    virtual std::unique_ptr<BlockTemplate> createNewBlockLegacy(const node::BlockCreateOptions& options = {}) { return createNewBlock(options, false); }
    virtual std::unique_ptr<BlockTemplate> createNewBlock(const node::BlockCreateOptions& options = {}, bool cooldown = true);
    virtual std::unique_ptr<BlockTemplate> createNewBlock2(const node::BlockCreateOptions& options) { return createNewBlock(options, false); }

    //! Mempool lookup. Results preserve input order, with nullptr for missing entries.
    virtual std::vector<CTransactionRef> getTransactionsByTxID(const std::vector<Txid>& txids);
    virtual std::vector<CTransactionRef> getTransactionsByWitnessID(const std::vector<Wtxid>& wtxids);

    //! Submit a complete block, including coinbase witness.
    //! Returns true only for a new accepted block, with reason/debug initialized on every call.
    virtual bool submitBlock(const CBlock& block, std::string& reason, std::string& debug);

    //! Permanently interrupt this client's waits without stopping other clients.
    virtual void interrupt();
    virtual bool checkBlock(const CBlock& block, const node::BlockCheckOptions& options, std::string& reason, std::string& debug);

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
