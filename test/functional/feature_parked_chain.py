#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Bitcoin developers
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Upstream-derived parking invariants adapted to Purity's supported policy.

Sources:
  Bitcoin ABC 335857dbf6fc706910c1e888ae3afd3cecf859e0:
    test/functional/abc_feature_parkedchain.py and abc-sync-chain.py
  Bitcoin Cash Node 07576013c91ff4a3a74acd85f189c69121cdad1b:
    test/functional/abc-parkedchain.py and abc-sync-chain.py
Historical state-order regressions: 4a9b35aa30806798214b5d4e33c577a4f16d9df5,
3cc9d160357adca8e001539f45385197ccae4954. No automatic unparking/Avalanche.
Active-chain manual parking is deferred; test its rejection and use inactive
branches for the park-first state permutations.
"""

from io import BytesIO
import random

from test_framework.messages import CBlock, CBlockHeader, msg_block, msg_headers
from test_framework.p2p import P2PInterface, p2p_lock
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than, assert_raises_rpc_error

STATE_ORDERS = {
    "state-a": ("invalidateblock", "parkblock", "unparkblock", "reconsiderblock"),
    "state-b": ("parkblock", "invalidateblock", "reconsiderblock", "unparkblock"),
    "state-c": ("invalidateblock", "parkblock", "reconsiderblock", "unparkblock"),
    "state-d": ("parkblock", "invalidateblock", "unparkblock", "reconsiderblock"),
}
SCENARIOS = ("active-park-rejected", *STATE_ORDERS, "descendants", "batch", "sync-batch", "restart")


class RequestedBatchPeer(P2PInterface):
    """Track requested blocks so IBD batches obey Core's in-flight limit."""
    def __init__(self):
        super().__init__()
        self.requested = set()

    def on_getdata(self, message):
        self.requested.update(inv.hash for inv in message.inv)


class ParkedChainTest(BitcoinTestFramework):
    def add_options(self, parser):
        parser.add_argument("--scenario", choices=("all", *SCENARIOS), default="all")

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [["-parkdeepreorg=0"], ["-parkdeepreorg=0"]]

    def chain_tip(self, block_hash):
        return next(tip for tip in self.nodes[0].getchaintips() if tip["hash"] == block_hash)

    def assert_state(self, fallback, competing, *, parked, invalid):
        expected_active = fallback if parked or invalid else competing
        assert_equal(self.nodes[0].getbestblockhash(), expected_active)
        tip = self.chain_tip(competing)
        assert_equal(tip["parked"], parked)
        if invalid:
            assert_equal(tip["status"], "invalid")
        else:
            assert tip["status"] != "invalid", tip
        assert_equal(self.chain_tip(expected_active)["status"], "active")
        assert_equal(self.chain_tip(expected_active)["parked"], False)

    def read_blocks(self, first_height, last_height):
        blocks = []
        for height in range(first_height, last_height + 1):
            block = CBlock()
            block.deserialize(BytesIO(bytes.fromhex(self.nodes[1].getblock(self.nodes[1].getblockhash(height), 0))))
            block.rehash()
            blocks.append(block)
        return blocks

    def submit_bodies(self, blocks):
        for block in blocks:
            # A parked body is stored without ConnectBlock/BlockChecked, so
            # Core's BIP22 submitblock reports inconclusive until activation.
            result = self.nodes[0].submitblock(block.serialize().hex())
            assert result in (None, "inconclusive"), result
            assert_equal(self.nodes[0].getblock(block.hash, 0), block.serialize().hex())

    def make_fork(self, rewind, extra):
        self.disconnect_nodes(0, 1)
        height = self.nodes[0].getblockcount()
        if rewind:
            self.generate(self.nodes[0], rewind, sync_fun=self.no_op)
        active = self.nodes[0].getbestblockhash()
        self.generate(self.nodes[1], rewind + extra, sync_fun=self.no_op)
        return active, self.read_blocks(height + 1, height + rewind + extra)

    def reconnect(self):
        self.connect_nodes(0, 1)
        self.sync_all()

    def enable_parking(self, extra_args=()):
        self.restart_node(0, extra_args=["-parkdeepreorg=1", "-parkreorgdepth=6", *extra_args])
        self.reconnect()

    def test_active_park_rejected(self):
        node = self.nodes[0]
        self.generate(node, 1)
        root = node.getbestblockhash()
        self.generate(node, 10)
        original = node.getbestblockhash()
        self.disconnect_nodes(0, 1)
        tips = node.getchaintips()
        for block_hash in (root, original):
            assert_raises_rpc_error(-1, "Cannot park a block on the active chain", node.parkblock, block_hash)
            assert_equal(node.getbestblockhash(), original)
            assert_equal(node.getchaintips(), tips)
        self.reconnect()

    def test_state_order(self, actions):
        node = self.nodes[0]
        pending_blocks = []
        if actions[0] == "parkblock":
            # B/D start with an inactive header branch. Deliver its higher-work
            # bodies only after parking so no active-chain rollback is needed.
            fallback, pending_blocks = self.make_fork(rewind=2, extra=1)
            for block in pending_blocks:
                node.submitheader(CBlockHeader(block).serialize().hex())
            root = pending_blocks[0].hash
            descendant = pending_blocks[-1].hash
        else:
            self.generate(node, 1)
            fallback = node.getbestblockhash()
            self.generate(node, 1)
            root = node.getbestblockhash()
            self.generate(node, 1)
            descendant = node.getbestblockhash()
            self.disconnect_nodes(0, 1)
        parked = invalid = False
        for action in actions:
            self.log.info("State transition: %s", action)
            getattr(node, action)(root)
            if action == "parkblock":
                parked = True
                self.submit_bodies(pending_blocks)
                pending_blocks = []
            elif action == "unparkblock":
                parked = False
            elif action == "invalidateblock":
                invalid = True
            elif action == "reconsiderblock":
                invalid = False
            self.assert_state(fallback, descendant, parked=parked, invalid=invalid)
        self.reconnect()

    def test_descendants(self):
        # Same parked-parent/descendant-unpark invariant as upstream, isolated
        # on a side branch so active-park rejection does not mask the result.
        active, blocks = self.make_fork(rewind=3, extra=-1)
        for block in blocks:
            self.nodes[0].submitheader(CBlockHeader(block).serialize().hex())
        self.nodes[0].parkblock(blocks[0].hash)
        self.submit_bodies(blocks)
        self.generate(self.nodes[1], 6, sync_fun=self.no_op)
        more = self.read_blocks(self.nodes[1].getblockcount() - 5, self.nodes[1].getblockcount())
        self.submit_bodies(more)
        competing = more[-1].hash
        self.assert_state(active, competing, parked=True, invalid=False)
        self.nodes[0].unparkblock(competing)
        self.assert_state(active, competing, parked=False, invalid=False)
        # Historical ABC 4459a350623b4a31f7a11a62213ee9a461fb54bf: a resolved
        # fork must not produce stale warnings. Purity's stable parking log is
        # checked here; ABC's absent Large-fork warning API is not invented.
        with self.nodes[0].assert_debug_log([], unexpected_msgs=["Parking block"]):
            self.generate(self.nodes[1], 1, sync_fun=self.no_op)
            self.submit_bodies(self.read_blocks(self.nodes[1].getblockcount(), self.nodes[1].getblockcount()))
        self.reconnect()

    def test_batch(self):
        # ABC 477f66718a833fa0ce33270c11cfa659b5f3ef24 / BCHN
        # 54d94d1a157c2fccfcfe11b6ffeae306213f7b88: new branch length is not
        # rewind depth. A 20-block batch with zero/one rewind must not park.
        self.enable_parking()
        for rewind in (0, 1):
            _, blocks = self.make_fork(rewind=rewind, extra=20)
            peer = self.nodes[0].add_p2p_connection(P2PInterface())
            with self.nodes[0].assert_debug_log([], unexpected_msgs=["Parking block"]):
                peer.send_and_ping(msg_headers([CBlockHeader(block) for block in blocks]))
                for block in reversed(blocks):
                    peer.send_and_ping(msg_block(block))
                    assert_equal(self.nodes[0].getblock(block.hash, 0), block.serialize().hex())
                assert_equal(self.nodes[0].getbestblockhash(), blocks[-1].hash)
                assert all(not tip["parked"] for tip in self.nodes[0].getchaintips())
            self.nodes[0].disconnect_p2ps()
            self.reconnect()

    def test_sync_batch(self):
        # ABC/BCHN abc-sync-chain.py (introduced in
        # e724cfe160dd8358910259ec6ffb328f69f0d703): 50 IBD + 50 post-IBD
        # blocks. Core requests at most one in-flight window at a time, so
        # shuffle each requested window instead of sending unrequested data.
        tip_work = int(self.nodes[0].getblockheader(self.nodes[0].getbestblockhash())["chainwork"], 16)
        self.restart_node(0, extra_args=["-parkdeepreorg=1", f"-minimumchainwork={tip_work + 100:x}", "-whitelist=noban@127.0.0.1"])
        assert_equal(self.nodes[0].getblockchaininfo()["initialblockdownload"], True)
        height = self.nodes[1].getblockcount()
        self.generate(self.nodes[1], 100, sync_fun=self.no_op)
        blocks = self.read_blocks(height + 1, height + 100)
        by_hash = {block.sha256: block for block in blocks}
        remaining = set(by_hash)
        arrivals = []
        peer = self.nodes[0].add_p2p_connection(RequestedBatchPeer())
        with self.nodes[0].assert_debug_log([], unexpected_msgs=["Parking block"]):
            peer.send_and_ping(msg_headers([CBlockHeader(block) for block in blocks]))
            while remaining:
                peer.wait_until(lambda: bool(peer.requested & remaining))
                with p2p_lock:
                    batch = sorted(peer.requested & remaining)
                random.shuffle(batch)
                for block_hash in batch:
                    block = by_hash[block_hash]
                    peer.send_and_ping(msg_block(block))
                    assert_equal(self.nodes[0].getblock(block.hash, 0), block.serialize().hex())
                    arrivals.append(block_hash)
                remaining.difference_update(batch)
            assert arrivals != [block.sha256 for block in blocks]
            assert_equal(self.nodes[0].getbestblockhash(), blocks[-1].hash)
            assert_equal(self.nodes[0].getblockchaininfo()["initialblockdownload"], False)
            assert all(not tip["parked"] for tip in self.nodes[0].getchaintips())
        self.nodes[0].disconnect_p2ps()
        self.reconnect()

    def test_restart(self):
        # Useful part of abf0af027c004a99dece4edb6e96fb43b4759d75: flags
        # survive disabling new parking. Purity excludes its automatic-unpark
        # ending, so even much more work must wait for the explicit RPC.
        self.enable_parking()
        active, blocks = self.make_fork(rewind=7, extra=1)
        with self.nodes[0].assert_debug_log(["Parking block"]):
            self.submit_bodies(blocks)
        self.assert_state(active, blocks[-1].hash, parked=True, invalid=False)
        self.restart_node(0, extra_args=["-parkdeepreorg=0"])
        self.assert_state(active, blocks[-1].hash, parked=True, invalid=False)
        height = self.nodes[1].getblockcount()
        self.generate(self.nodes[1], 30, sync_fun=self.no_op)
        self.submit_bodies(self.read_blocks(height + 1, height + 30))
        competing = self.nodes[1].getbestblockhash()
        assert_greater_than(int(self.nodes[1].getblockheader(competing)["chainwork"], 16),
                            int(self.nodes[0].getblockheader(active)["chainwork"], 16))
        self.assert_state(active, competing, parked=True, invalid=False)
        self.nodes[0].unparkblock(blocks[0].hash)
        self.assert_state(active, competing, parked=False, invalid=False)
        self.reconnect()

    def run_test(self):
        self.generate(self.nodes[0], 10)
        tests = {
            "active-park-rejected": self.test_active_park_rejected,
            "descendants": self.test_descendants,
            "batch": self.test_batch,
            "sync-batch": self.test_sync_batch,
            "restart": self.test_restart,
        }
        selected = SCENARIOS if self.options.scenario == "all" else (self.options.scenario,)
        for scenario in selected:
            self.log.info("Upstream parking scenario: %s", scenario)
            if scenario in STATE_ORDERS:
                self.test_state_order(STATE_ORDERS[scenario])
            else:
                tests[scenario]()


if __name__ == "__main__":
    ParkedChainTest(__file__).main()
