// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <common/args.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <interfaces/init.h>
#include <interfaces/mining.h>
#include <ipc/capnp/protocol.h>
#include <ipc/process.h>
#include <ipc/protocol.h>
#include <mp/proxy-types.h>
#include <node/types.h>
#include <pow.h>
#include <rpc/server.h>
#include <rpc/request.h>
#include <consensus/consensus.h>
#include <test/ipc_test.capnp.h>
#include <test/ipc_test.capnp.proxy.h>
#include <test/ipc_test.capnp.proxy-types.h>
#include <test/ipc_test.h>
#include <tinyformat.h>

#include <future>
#include <thread>
#include <kj/common.h>
#include <kj/memory.h>
#include <kj/test.h>
#include <stdexcept>

#include <boost/test/unit_test.hpp>

//! Remote init class.
class TestInit : public interfaces::Init
{
public:
    explicit TestInit(node::NodeContext* node = nullptr) : m_node(node) {}
    std::unique_ptr<interfaces::Echo> makeEcho() override { return interfaces::MakeEcho(); }
    std::unique_ptr<interfaces::Mining> makeMining() override { return m_node ? interfaces::MakeMining(*m_node) : nullptr; }
    std::unique_ptr<interfaces::Rpc> makeRpc() override { return m_node ? interfaces::MakeRpc(*m_node) : nullptr; }
    node::NodeContext* m_node;
};

//! Generate a temporary path with temp_directory_path and mkstemp
static std::string TempPath(std::string_view pattern)
{
    std::string temp{fs::PathToString(fs::path{fs::temp_directory_path()} / fs::PathFromString(std::string{pattern}))};
    temp.push_back('\0');
    int fd{mkstemp(temp.data())};
    BOOST_CHECK_GE(fd, 0);
    BOOST_CHECK_EQUAL(close(fd), 0);
    temp.resize(temp.size() - 1);
    fs::remove(fs::PathFromString(temp));
    return temp;
}

//! Unit test that tests execution of IPC calls without actually creating a
//! separate process. This test is primarily intended to verify behavior of type
//! conversion code that converts C++ objects to Cap'n Proto messages and vice
//! versa.
//!
//! The test creates a thread which creates a FooImplementation object (defined
//! in ipc_test.h) and a two-way pipe accepting IPC requests which call methods
//! on the object through FooInterface (defined in ipc_test.capnp).
void IpcPipeTest()
{
    // Setup: create FooImplemention object and listen for FooInterface requests
    std::promise<std::unique_ptr<mp::ProxyClient<gen::FooInterface>>> foo_promise;
    std::function<void()> disconnect_client;
    std::thread thread([&]() {
        mp::EventLoop loop("IpcPipeTest", [](bool raise, const std::string& log) { LogPrintf("LOG%i: %s\n", raise, log); });
        auto pipe = loop.m_io_context.provider->newTwoWayPipe();

        auto connection_client = std::make_unique<mp::Connection>(loop, kj::mv(pipe.ends[0]));
        auto foo_client = std::make_unique<mp::ProxyClient<gen::FooInterface>>(
            connection_client->m_rpc_system->bootstrap(mp::ServerVatId().vat_id).castAs<gen::FooInterface>(),
            connection_client.get(), /* destroy_connection= */ false);
        foo_promise.set_value(std::move(foo_client));
        disconnect_client = [&] { loop.sync([&] { connection_client.reset(); }); };

        auto connection_server = std::make_unique<mp::Connection>(loop, kj::mv(pipe.ends[1]), [&](mp::Connection& connection) {
            auto foo_server = kj::heap<mp::ProxyServer<gen::FooInterface>>(std::make_shared<FooImplementation>(), connection);
            return capnp::Capability::Client(kj::mv(foo_server));
        });
        connection_server->onDisconnect([&] { connection_server.reset(); });
        loop.loop();
    });
    std::unique_ptr<mp::ProxyClient<gen::FooInterface>> foo{foo_promise.get_future().get()};

    // Test: make sure arguments were sent and return value is received
    BOOST_CHECK_EQUAL(foo->add(1, 2), 3);

    COutPoint txout1{Txid::FromUint256(uint256{100}), 200};
    COutPoint txout2{foo->passOutPoint(txout1)};
    BOOST_CHECK(txout1 == txout2);

    UniValue uni1{UniValue::VOBJ};
    uni1.pushKV("i", 1);
    uni1.pushKV("s", "two");
    UniValue uni2{foo->passUniValue(uni1)};
    BOOST_CHECK_EQUAL(uni1.write(), uni2.write());

    CMutableTransaction mtx;
    mtx.version = 2;
    mtx.nLockTime = 3;
    mtx.vin.emplace_back(txout1);
    mtx.vout.emplace_back(COIN, CScript());
    CTransactionRef tx1{MakeTransactionRef(mtx)};
    CTransactionRef tx2{foo->passTransaction(tx1)};
    BOOST_CHECK(*Assert(tx1) == *Assert(tx2));

    std::vector<char> vec1{'H', 'e', 'l', 'l', 'o'};
    std::vector<char> vec2{foo->passVectorChar(vec1)};
    BOOST_CHECK_EQUAL(std::string_view(vec1.begin(), vec1.end()), std::string_view(vec2.begin(), vec2.end()));

    BlockValidationState bs1;
    bs1.Invalid(BlockValidationResult::BLOCK_CHECKPOINT, "reject reason", "debug message");
    BlockValidationState bs2{foo->passBlockState(bs1)};
    BOOST_CHECK_EQUAL(bs1.IsValid(), bs2.IsValid());
    BOOST_CHECK_EQUAL(bs1.IsError(), bs2.IsError());
    BOOST_CHECK_EQUAL(bs1.IsInvalid(), bs2.IsInvalid());
    BOOST_CHECK_EQUAL(static_cast<int>(bs1.GetResult()), static_cast<int>(bs2.GetResult()));
    BOOST_CHECK_EQUAL(bs1.GetRejectReason(), bs2.GetRejectReason());
    BOOST_CHECK_EQUAL(bs1.GetDebugMessage(), bs2.GetDebugMessage());

    BlockValidationState bs3;
    BlockValidationState bs4{foo->passBlockState(bs3)};
    BOOST_CHECK_EQUAL(bs3.IsValid(), bs4.IsValid());
    BOOST_CHECK_EQUAL(bs3.IsError(), bs4.IsError());
    BOOST_CHECK_EQUAL(bs3.IsInvalid(), bs4.IsInvalid());
    BOOST_CHECK_EQUAL(static_cast<int>(bs3.GetResult()), static_cast<int>(bs4.GetResult()));
    BOOST_CHECK_EQUAL(bs3.GetRejectReason(), bs4.GetRejectReason());
    BOOST_CHECK_EQUAL(bs3.GetDebugMessage(), bs4.GetDebugMessage());

    node::BlockCreateOptions options;
    options.use_mempool = false;
    options.block_reserved_weight = 9000;
    options.coinbase_output_max_additional_sigops = 42;
    BOOST_CHECK(foo->passBlockOptions(options) == options);
    BOOST_CHECK(foo->passBlockOptions(node::BlockCreateOptions{}) == node::BlockCreateOptions{});

    capnp::MallocMessageBuilder legacy;
    auto params = legacy.initRoot<gen::FooInterface::PassBlockOptionsParams>();
    auto old_options = params.initArg();
    old_options.setUseMempool(true);
    old_options.setBlockReservedWeight(DEFAULT_BLOCK_RESERVED_WEIGHT);
    old_options.setCoinbaseOutputMaxAdditionalSigops(400);
    auto reader = params.asReader();
    using Accessor = mp::Accessor<mp::ipc_test_fields::Arg, mp::FIELD_IN | mp::FIELD_BOXED>;
    mp::InvokeContext context{*foo->m_context.connection};
    node::BlockCreateOptions decoded;
    mp::ReadField(mp::TypeList<node::BlockCreateOptions>{}, context,
        mp::Make<mp::StructField, Accessor>(reader), mp::ReadDestUpdate(decoded));
    const node::BlockCreateOptions defaults;
    BOOST_CHECK_EQUAL(decoded.use_mempool, defaults.use_mempool);
    BOOST_CHECK_EQUAL(decoded.block_reserved_size, defaults.block_reserved_size);
    BOOST_CHECK_EQUAL(decoded.block_reserved_weight, defaults.block_reserved_weight);
    BOOST_CHECK_EQUAL(decoded.coinbase_output_max_additional_sigops, defaults.coinbase_output_max_additional_sigops);
    BOOST_CHECK(decoded.coinbase_output_script == defaults.coinbase_output_script);
    BOOST_CHECK_EQUAL(decoded.nBlockMaxSize, defaults.nBlockMaxSize);
    BOOST_CHECK_EQUAL(decoded.nBlockMaxWeight, defaults.nBlockMaxWeight);
    BOOST_CHECK_EQUAL(decoded.blockMinFeeRate.GetFeePerK(), defaults.blockMinFeeRate.GetFeePerK());
    BOOST_CHECK_EQUAL(decoded.test_block_validity, defaults.test_block_validity);
    BOOST_CHECK_EQUAL(decoded.print_modified_fee, defaults.print_modified_fee);

    auto script1{CScript() << OP_11};
    auto script2{foo->passScript(script1)};
    BOOST_CHECK_EQUAL(HexStr(script1), HexStr(script2));

    // Test cleanup: disconnect pipe and join thread
    disconnect_client();
    foo.reset();
    thread.join();
}

//! Test ipc::Protocol connect() and serve() methods connecting over a socketpair.
void IpcSocketPairTest(node::NodeContext* node)
{
    int fds[2];
    BOOST_CHECK_EQUAL(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    std::unique_ptr<interfaces::Init> init{std::make_unique<TestInit>(node)};
    std::unique_ptr<ipc::Protocol> protocol{ipc::capnp::MakeCapnpProtocol()};
    std::promise<void> promise;
    std::jthread thread([&]() {
        protocol->serve(fds[0], "test-serve", *init, [&] { promise.set_value(); });
    });
    promise.get_future().wait();
    std::unique_ptr<interfaces::Init> remote_init{protocol->connect(fds[1], "test-connect")};
    std::unique_ptr<interfaces::Echo> remote_echo{remote_init->makeEcho()};
    BOOST_CHECK_EQUAL(remote_echo->echo("echo test"), "echo test");
    protocol->disconnectIncoming();
    BOOST_CHECK_EQUAL(remote_echo->echo("parent remains connected"), "parent remains connected");
    if (node) {
        auto remote_mining = remote_init->makeMining();
        auto local_mining = interfaces::MakeMining(*node);
        BOOST_REQUIRE(remote_mining);
        BOOST_CHECK(remote_mining->isTestChain());
        auto local_tip = local_mining->getTip();
        auto remote_tip = remote_mining->getTip();
        BOOST_REQUIRE(local_tip);
        BOOST_REQUIRE(remote_tip);
        BOOST_CHECK(local_tip->hash == remote_tip->hash);
        BOOST_CHECK_EQUAL(local_tip->height, remote_tip->height);
        node::BlockCreateOptions options;
        options.use_mempool = false;
        options.block_reserved_weight = 9000;
        auto local = local_mining->createNewBlock(options, false);
        auto remote = remote_mining->createNewBlock(options, false);
        BOOST_REQUIRE(local);
        BOOST_REQUIRE(remote);
        BOOST_CHECK(remote->getBlock().GetHash() == local->getBlock().GetHash());
        BOOST_CHECK(remote->getBlockHeader().GetHash() == local->getBlockHeader().GetHash());
        BOOST_CHECK(remote->getTxFees() == local->getTxFees());
        BOOST_CHECK(remote->getTxSigops() == local->getTxSigops());
        BOOST_CHECK_THROW(remote->getTxCoinAgePriorities(), std::runtime_error);
        BOOST_CHECK_EQUAL(remote->getTxFees().size(), remote->getBlock().vtx.size() - 1);
        BOOST_CHECK_EQUAL(remote->getTxSigops().size(), remote->getBlock().vtx.size() - 1);
        const auto coinbase = remote->getCoinbaseTx();
        BOOST_CHECK_EQUAL(coinbase.version, local->getCoinbaseTx().version);
        BOOST_CHECK(coinbase.witness == local->getCoinbaseTx().witness);
        BOOST_CHECK(coinbase.required_outputs == local->getCoinbaseTx().required_outputs);
        BOOST_CHECK_EQUAL(coinbase.block_reward_remaining, local->getCoinbaseTx().block_reward_remaining);
        BOOST_CHECK(remote->getCoinbaseTransaction()->GetHash() == local->getCoinbaseTransaction()->GetHash());
        BOOST_CHECK_THROW(remote->getCoinbaseCommitment(), std::runtime_error);
        BOOST_CHECK_THROW(remote->getWitnessCommitmentIndex(), std::runtime_error);
        BOOST_CHECK(remote->getCoinbaseMerklePath() == local->getCoinbaseMerklePath());
        auto local_default = local_mining->createNewBlockLegacy();
        BOOST_REQUIRE(local_default);
        auto remote_default = remote_mining->createNewBlockLegacy();
        BOOST_REQUIRE(remote_default);
        CBlock candidate = remote->getBlock();
        CMutableTransaction payout{*candidate.vtx[0]};
        payout.vout[0].nValue -= 1;
        payout.vout.emplace_back(1, CScript() << OP_TRUE);
        candidate.vtx[0] = MakeTransactionRef(payout);
        candidate.hashMerkleRoot = BlockMerkleRoot(candidate);
        while (!CheckProofOfWork(candidate.GetHash(), candidate.nBits, Params().GetConsensus())) ++candidate.nNonce;
        std::string reason{"old"}, debug{"old"};
        node::BlockCheckOptions check;
        BOOST_CHECK(remote_mining->checkBlock(candidate, check, reason, debug));
        BOOST_CHECK(reason.empty()); BOOST_CHECK(debug.empty());
        CBlock invalid{candidate};
        CMutableTransaction broken_coinbase{*candidate.vtx[0]};
        broken_coinbase.vin[0].scriptWitness.SetNull();
        invalid.vtx[0] = MakeTransactionRef(broken_coinbase);
        check.check_pow = false;
        CBlock bad_root{candidate}; bad_root.hashMerkleRoot = uint256{1};
        BOOST_CHECK(!remote_mining->checkBlock(bad_root, check, reason, debug));
        BOOST_CHECK_EQUAL(reason, "bad-txnmrklroot");
        check.check_merkle_root = false;
        BOOST_CHECK(remote_mining->checkBlock(bad_root, check, reason, debug));
        check.check_merkle_root = true;
        CBlock bad_parent{candidate}; bad_parent.hashPrevBlock = uint256{1};
        BOOST_CHECK(!remote_mining->checkBlock(bad_parent, check, reason, debug));
        BOOST_CHECK_EQUAL(reason, "inconclusive-not-best-prevblk");
        BOOST_CHECK(!remote_mining->checkBlock(invalid, check, reason, debug));
        BOOST_CHECK_EQUAL(reason, "bad-witness-nonce-size");
        BOOST_CHECK(remote->submitSolution(candidate.nVersion, candidate.nTime, candidate.nNonce, candidate.vtx[0], reason, debug));
        BOOST_CHECK(reason.empty()); BOOST_CHECK(debug.empty());
        BOOST_CHECK(!remote->submitSolution(candidate.nVersion, candidate.nTime, candidate.nNonce, candidate.vtx[0], reason, debug));
        BOOST_CHECK_EQUAL(reason, "duplicate");
        BOOST_CHECK(!remote_mining->submitBlock(candidate, reason, debug));
        BOOST_CHECK_EQUAL(reason, "duplicate");
        BOOST_CHECK(remote->getBlock().GetHash() == candidate.GetHash());
        BOOST_CHECK_EQUAL(remote->getCoinbaseTx().block_reward_remaining, coinbase.block_reward_remaining);
        BOOST_CHECK(remote->getCoinbaseTx().required_outputs == coinbase.required_outputs);
        BOOST_CHECK_EXCEPTION(remote->submitSolutionOld7(candidate.nVersion, candidate.nTime, candidate.nNonce, candidate.vtx[0]), std::exception,
                              [](const std::exception& error) { return std::string{error.what()}.find("Old submitSolution (@7)") != std::string::npos; });
        BOOST_CHECK_EXCEPTION(remote_init->makeMiningOld2(), std::exception,
                              [](const std::exception& error) { return std::string{error.what()}.find("Old makeMining (@2)") != std::string::npos; });
        auto updated = remote->waitNext(node::BlockWaitOptions{.timeout = MillisecondsDouble{0}});
        BOOST_REQUIRE(updated);
        BOOST_CHECK(updated->getBlock().hashPrevBlock == candidate.GetHash());
        BOOST_CHECK(!updated->waitNext(node::BlockWaitOptions{.timeout = MillisecondsDouble{0}}));
        auto waiting = std::async(std::launch::async, [&] { return updated->waitNext(); });
        updated->interruptWait();
        BOOST_REQUIRE(waiting.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
        BOOST_CHECK(!waiting.get());
        auto fresh = remote_default->waitNext(node::BlockWaitOptions{.timeout = MillisecondsDouble{1000}});
        BOOST_REQUIRE(fresh);
        BOOST_CHECK(fresh->getBlock().hashPrevBlock == candidate.GetHash());
        const auto txids = remote_mining->getTransactionsByTxID({Txid::FromUint256(uint256{1})});
        BOOST_REQUIRE_EQUAL(txids.size(), 1);
        BOOST_CHECK(!txids[0]);
        const auto wtxids = remote_mining->getTransactionsByWitnessID({Wtxid::FromUint256(uint256{1})});
        BOOST_REQUIRE_EQUAL(wtxids.size(), 1);
        BOOST_CHECK(!wtxids[0]);
        options.block_reserved_weight = MAX_BLOCK_WEIGHT + 1;
        BOOST_CHECK_THROW(remote_mining->createNewBlock(options, false), std::exception);
        if (RPCIsInWarmup(nullptr)) SetRPCWarmupFinished();
        auto rpc = remote_init->makeRpc();
        BOOST_REQUIRE(rpc);
        UniValue request; BOOST_REQUIRE(request.read(R"({"jsonrpc":"2.0","id":1,"method":"getblockcount","params":[]})"));
        gArgs.ForceSetArg("-ipcrpc", "0");
        BOOST_CHECK_THROW(rpc->executeRpc(request, "/", "ipc-test"), std::exception);
        gArgs.ForceSetArg("-ipcrpc", "1");
        const auto response = rpc->executeRpc(request, "/", "ipc-test");
        BOOST_CHECK(response["error"].isNull());
        BOOST_CHECK_EQUAL(response["result"].getInt<int>(), local_tip->height + 1);
        rpc.reset();
        const auto submitted_tip = local_mining->getTip();
        BOOST_REQUIRE(submitted_tip);
        BOOST_CHECK(submitted_tip->hash == candidate.GetHash());
        BOOST_CHECK_EQUAL(submitted_tip->height, local_tip->height + 1);
    }
    remote_echo.reset();
    remote_init.reset();
    thread.join();
}

//! Test ipc::Process bind() and connect() methods connecting over a unix socket.
void IpcSocketTest(const fs::path& datadir)
{
    std::unique_ptr<interfaces::Init> init{std::make_unique<TestInit>()};
    std::unique_ptr<ipc::Protocol> protocol{ipc::capnp::MakeCapnpProtocol()};
    std::unique_ptr<ipc::Process> process{ipc::MakeProcess()};

    std::string invalid_bind{"invalid:"};
    BOOST_CHECK_THROW(process->bind(datadir, "test_bitcoin", invalid_bind), std::invalid_argument);
    BOOST_CHECK_THROW(process->connect(datadir, "test_bitcoin", invalid_bind), std::invalid_argument);

    auto bind_and_listen{[&](const std::string& bind_address) {
        std::string address{bind_address};
        int serve_fd = process->bind(datadir, "test_bitcoin", address);
        BOOST_CHECK_GE(serve_fd, 0);
        BOOST_CHECK_EQUAL(address, bind_address);
        protocol->listen(serve_fd, "test-serve", *init);
    }};

    auto connect_and_test{[&](const std::string& connect_address) {
        std::string address{connect_address};
        int connect_fd{process->connect(datadir, "test_bitcoin", address)};
        BOOST_CHECK_EQUAL(address, connect_address);
        std::unique_ptr<interfaces::Init> remote_init{protocol->connect(connect_fd, "test-connect")};
        std::unique_ptr<interfaces::Echo> remote_echo{remote_init->makeEcho()};
        BOOST_CHECK_EQUAL(remote_echo->echo("echo test"), "echo test");
        protocol->disconnectIncoming();
        BOOST_CHECK_THROW(remote_echo->echo("disconnected"), std::exception);
    }};

    // Need to specify explicit socket addresses outside the data directory, because the data
    // directory path is so long that the default socket address and any other
    // addresses in the data directory would fail with errors like:
    //   Address 'unix' path '"/tmp/test_common_Bitcoin Core/ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff/test_bitcoin.sock"' exceeded maximum socket path length
    std::vector<std::string> addresses{
        strprintf("unix:%s", TempPath("bitcoin_sock0_XXXXXX")),
        strprintf("unix:%s", TempPath("bitcoin_sock1_XXXXXX")),
    };

    // Bind and listen on multiple addresses
    for (const auto& address : addresses) {
        bind_and_listen(address);
    }

    // Connect and test each address multiple times.
    for (int i : {0, 1, 0, 0, 1}) {
        connect_and_test(addresses[i]);
    }
}

namespace mp {
void CustomBuildMessage(InvokeContext& invoke_context,
                        const BlockValidationState& src,
                        gen::BlockValidationState::Builder&& builder)
{
    if (src.IsValid()) {
        builder.setMode(0);
    } else if (src.IsInvalid()) {
        builder.setMode(1);
    } else if (src.IsError()) {
        builder.setMode(2);
    } else {
        assert(false);
    }
    builder.setResult(static_cast<int>(src.GetResult()));
    builder.setRejectReason(src.GetRejectReason());
    builder.setDebugMessage(src.GetDebugMessage());
}

void CustomReadMessage(InvokeContext& invoke_context,
                       const gen::BlockValidationState::Reader& reader,
                       BlockValidationState& dest)
{
    if (reader.getMode() == 0) {
        assert(reader.getResult() == 0);
        assert(reader.getRejectReason().size() == 0);
        assert(reader.getDebugMessage().size() == 0);
    } else if (reader.getMode() == 1) {
        dest.Invalid(static_cast<BlockValidationResult>(reader.getResult()), reader.getRejectReason(), reader.getDebugMessage());
    } else if (reader.getMode() == 2) {
        assert(reader.getResult() == 0);
        dest.Error(reader.getRejectReason());
        assert(reader.getDebugMessage().size() == 0);
    } else {
        assert(false);
    }
}
} // namespace mp
