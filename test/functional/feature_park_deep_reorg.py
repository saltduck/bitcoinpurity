#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Park competing chains that would rewind more than -parkreorgdepth blocks.

Deep-reorg parking is a node-local chain-selection policy, not consensus.
The default threshold is 6: a 6-block reorg proceeds automatically, a
7-block reorg is parked for manual review.
"""

from io import BytesIO
from pathlib import Path
import struct

from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import CBlock, CBlockHeader, MAGIC_BYTES, msg_block, msg_headers
from test_framework.p2p import P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_node import ErrorMatch
from test_framework.util import assert_equal, assert_greater_than


class ParkDeepReorgTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-parkdeepreorg=1", "-parkreorgdepth=6", "-checkblockindex=1"],
            ["-parkdeepreorg=0", "-checkblockindex=1"],
        ]

    def setup_network(self):
        self.setup_nodes()
        self.connect_nodes(0, 1)
        self.sync_all()

    def _build_fork(self, short_idx, long_idx, rewind, extra=1):
        """Mine a competing fork that would rewind `rewind` blocks on short_idx."""
        self.disconnect_nodes(short_idx, long_idx)
        fork_height = self.nodes[short_idx].getblockcount()
        self.generate(self.nodes[short_idx], rewind, sync_fun=self.no_op)
        self.generate(self.nodes[long_idx], rewind + extra, sync_fun=self.no_op)
        fork_hash = self.nodes[long_idx].getblockhash(fork_height + 1)
        return fork_hash, self.nodes[short_idx].getblockcount(), self.nodes[long_idx].getblockcount()

    def _connect_expect_follow(self, short_idx, long_idx, expected_height):
        self.connect_nodes(short_idx, long_idx)
        self.wait_until(lambda: self.nodes[short_idx].getblockcount() == expected_height, timeout=10)
        assert_equal(self.nodes[short_idx].getbestblockhash(), self.nodes[long_idx].getbestblockhash())
        assert_equal(next(t for t in self.nodes[short_idx].getchaintips() if t["status"] == "active")["parked"], False)

    def _connect_expect_parked(self, short_idx, long_idx, expected_height, competing_tip):
        original_tip = self.nodes[short_idx].getbestblockhash()
        with self.nodes[short_idx].assert_debug_log(["Parking block"], timeout=10):
            self.connect_nodes(short_idx, long_idx)
        # Headers expose the tip before all bodies arrive. Wait for the full
        # branch so a later body cannot park it again after unparkblock.
        self.wait_until(lambda: any(t["hash"] == competing_tip and t["status"] == "valid-headers"
                                   for t in self.nodes[short_idx].getchaintips()), timeout=10)
        assert_equal(self.nodes[short_idx].getblockcount(), expected_height)
        assert_equal(self.nodes[short_idx].getbestblockhash(), original_tip)
        assert_equal(next(t for t in self.nodes[short_idx].getchaintips() if t["hash"] == competing_tip)["parked"], True)

    def test_invalid_parkreorgdepth(self):
        self.log.info("Reject -parkreorgdepth values below 1")
        self.stop_node(0)
        self.nodes[0].assert_start_raises_init_error(
            extra_args=["-parkreorgdepth=0"],
            expected_msg="Error: -parkreorgdepth must be at least 1 (got 0); use -parkdeepreorg=0 to disable parking",
            match=ErrorMatch.FULL_TEXT,
        )
        self.nodes[0].assert_start_raises_init_error(
            extra_args=["-parkreorgdepth=-1"],
            expected_msg="Error: -parkreorgdepth must be at least 1 (got -1); use -parkdeepreorg=0 to disable parking",
            match=ErrorMatch.FULL_TEXT,
        )
        self.start_node(0)
        self.connect_nodes(0, 1)

    def test_default_boundary(self):
        # Adapted from ABC 335857dbf6fc706910c1e888ae3afd3cecf859e0
        # abc_feature_parkedchain.py and BCHN
        # 07576013c91ff4a3a74acd85f189c69121cdad1b abc-parkedchain.py.
        # Preserve Purity's rewind > 6 rule and explicit unpark, rather than
        # upstream's numeric thresholds or delayed PoW-based recovery.
        for rewind in (1, 2, 5, 6):
            self.log.info("Default depth=6: %d-block rewind follows automatically", rewind)
            _, _, long_height = self._build_fork(0, 1, rewind=rewind)
            self._connect_expect_follow(0, 1, long_height)

        for rewind in (7, 8, 20):
            self.log.info("Default depth=6: %d-block rewind is parked", rewind)
            fork_hash, short_height, _ = self._build_fork(0, 1, rewind=rewind)
            competing_tip = self.nodes[1].getbestblockhash()
            self._connect_expect_parked(0, 1, short_height, competing_tip)
            assert_equal(next(t for t in self.nodes[0].getchaintips() if t["status"] == "active")["parked"], False)
            self.nodes[0].unparkblock(fork_hash)
            self.wait_until(lambda: self.nodes[0].getblockcount() == self.nodes[1].getblockcount(), timeout=10)
            assert_equal(self.nodes[0].getbestblockhash(), competing_tip)
            assert_equal(next(t for t in self.nodes[0].getchaintips() if t["status"] == "active")["parked"], False)

    def test_custom_threshold(self):
        self.log.info("Custom parkreorgdepth=4: 4-block reorg is not parked")
        self.restart_node(0, extra_args=["-parkdeepreorg=1", "-parkreorgdepth=4"])
        self.connect_nodes(0, 1)
        self.sync_all()
        _, _, long_height = self._build_fork(0, 1, rewind=4)
        self._connect_expect_follow(0, 1, long_height)

        self.log.info("Custom parkreorgdepth=4: 5-block reorg is parked")
        fork_hash, short_height, _ = self._build_fork(0, 1, rewind=5)
        competing_tip = self.nodes[1].getbestblockhash()
        self._connect_expect_parked(0, 1, short_height, competing_tip)
        assert_equal(next(t for t in self.nodes[0].getchaintips() if t["status"] == "active")["parked"], False)
        self.nodes[0].unparkblock(fork_hash)
        self.wait_until(lambda: self.nodes[0].getblockcount() == self.nodes[1].getblockcount(), timeout=10)
        assert_equal(self.nodes[0].getbestblockhash(), self.nodes[1].getbestblockhash())
        assert_equal(next(t for t in self.nodes[0].getchaintips() if t["status"] == "active")["parked"], False)

    def test_headers_first_missing_ancestor_last(self):
        self.log.info("Headers first, missing ancestor last: park independently of pblock")
        self.restart_node(0, extra_args=["-parkdeepreorg=1", "-parkreorgdepth=6", "-checkblockindex=1"])
        self.connect_nodes(0, 1)
        self.sync_all()
        fork_hash, short_height, long_height = self._build_fork(0, 1, rewind=7)
        original_tip = self.nodes[0].getbestblockhash()
        blocks = []
        for height in range(short_height - 6, long_height + 1):
            block = CBlock()
            block.deserialize(BytesIO(bytes.fromhex(self.nodes[1].getblock(self.nodes[1].getblockhash(height), 0))))
            block.rehash()
            blocks.append(block)
        assert_greater_than(int(self.nodes[1].getblockheader(self.nodes[1].getbestblockhash())["chainwork"], 16),
                            int(self.nodes[0].getblockheader(original_tip)["chainwork"], 16))
        peer = self.nodes[0].add_p2p_connection(P2PInterface())
        peer.send_and_ping(msg_headers([CBlockHeader(block) for block in blocks]))
        assert_equal(next(t for t in self.nodes[0].getchaintips() if t["hash"] == blocks[-1].hash)["parked"], False)
        # The old pblock/tip predicate cannot park while B1 is missing: the
        # descendants have data but are unlinked. Acceptance must park earlier.
        with self.nodes[0].assert_debug_log(["Parking block"], timeout=10):
            for block in blocks[1:]:
                peer.send_and_ping(msg_block(block))
                assert_equal(self.nodes[0].getblock(block.hash, 0), block.serialize().hex())
                assert_equal(next(t for t in self.nodes[0].getchaintips() if t["hash"] == blocks[-1].hash)["parked"], True)
        assert_equal(self.nodes[0].getbestblockhash(), original_tip)
        with self.nodes[0].assert_debug_log([], unexpected_msgs=["Parking block"]):
            peer.send_and_ping(msg_block(blocks[0]))
        assert_equal(self.nodes[0].getbestblockhash(), original_tip)
        tip = next(t for t in self.nodes[0].getchaintips() if t["hash"] == blocks[-1].hash)
        assert_equal(tip["parked"], True)
        assert_equal(next(t for t in self.nodes[0].getchaintips() if t["status"] == "active")["parked"], False)
        self.nodes[0].unparkblock(fork_hash)
        assert_equal(self.nodes[0].getbestblockhash(), blocks[-1].hash)
        assert_equal(next(t for t in self.nodes[0].getchaintips() if t["status"] == "active")["parked"], False)
        self.nodes[0].disconnect_p2ps()
        self.connect_nodes(0, 1)
        self.sync_all()

    def test_null_pblock_reconsider(self):
        self.log.info("Persist parking through restart, reconsider(nullptr), and chainstate rebuild")
        fork_hash, short_height, _ = self._build_fork(0, 1, rewind=7)
        original_tip = self.nodes[0].getbestblockhash()
        competing_tip = self.nodes[1].getbestblockhash()
        self._connect_expect_parked(0, 1, short_height, competing_tip)
        self.disconnect_nodes(0, 1)
        header_block = create_block(int(competing_tip, 16), create_coinbase(self.nodes[1].getblockcount() + 1), self.nodes[1].getblockheader(competing_tip)["time"] + 1, version=4)
        header_block.solve()
        self.nodes[0].submitheader(header_block.serialize().hex())
        self.nodes[0].invalidateblock(competing_tip)
        self.nodes[0].invalidateblock(fork_hash)
        self.nodes[0].invalidateblock(competing_tip)
        self.restart_node(0, extra_args=["-parkdeepreorg=1", "-parkreorgdepth=6", "-checkblockindex=1"])
        tip = next(t for t in self.nodes[0].getchaintips() if t["hash"] == header_block.hash)
        assert_equal(tip["status"], "invalid")
        assert_equal(tip["parked"], True)
        assert_equal(self.nodes[0].getbestblockhash(), original_tip)
        for extra_args in ([], ["-reindex-chainstate"]):
            self.restart_node(0, extra_args=["-parkdeepreorg=1", "-parkreorgdepth=6", "-checkblockindex=1", *extra_args])
            # reconsiderblock calls ABC(nullptr); clearing invalidity must not
            # clear the independent persisted parking decision.
            self.nodes[0].reconsiderblock(header_block.hash)
            self.nodes[0].reconsiderblock(header_block.hash)
            assert_equal(self.nodes[0].getbestblockhash(), original_tip)
            tip = next(t for t in self.nodes[0].getchaintips() if t["hash"] == header_block.hash)
            assert_equal(tip["parked"], True)
            assert tip["status"] != "invalid"
        self.nodes[0].unparkblock(fork_hash)
        assert_equal(self.nodes[0].getbestblockhash(), competing_tip)
        assert_equal(next(t for t in self.nodes[0].getchaintips() if t["status"] == "active")["parked"], False)
        self.connect_nodes(0, 1)
        self.sync_all()

    def test_out_of_order_import(self):
        self.log.info("Import descendant bodies before the missing ancestor with persisted headers")
        fork_hash, short_height, long_height = self._build_fork(0, 1, rewind=7)
        original_tip = self.nodes[0].getbestblockhash()
        blocks = []
        for height in range(short_height - 6, long_height + 1):
            block = CBlock()
            block.deserialize(BytesIO(bytes.fromhex(self.nodes[1].getblock(self.nodes[1].getblockhash(height), 0))))
            block.rehash()
            blocks.append(block)
        peer = self.nodes[0].add_p2p_connection(P2PInterface())
        peer.send_and_ping(msg_headers([CBlockHeader(block) for block in blocks]))
        self.nodes[0].disconnect_p2ps()
        bootstrap = Path(self.options.tmpdir) / "parking-bootstrap.dat"
        with bootstrap.open("wb") as stream:
            for block in [*blocks[1:], blocks[0]]:
                data = block.serialize()
                stream.write(MAGIC_BYTES["regtest"] + struct.pack("<I", len(data)) + data)
        self.restart_node(0, extra_args=["-parkdeepreorg=1", "-parkreorgdepth=6", f"-loadblock={bootstrap}"])
        assert_equal(self.nodes[0].getbestblockhash(), original_tip)
        assert_equal(next(t for t in self.nodes[0].getchaintips() if t["hash"] == blocks[-1].hash)["parked"], True)
        # Import calls ABC(nullptr) after all bodies have been accepted.
        self.nodes[0].unparkblock(fork_hash)
        assert_equal(self.nodes[0].getbestblockhash(), blocks[-1].hash)
        self.connect_nodes(0, 1)
        self.sync_all()

    def test_reindex_rebuild(self):
        self.log.info("Full reindex rebuilds local parking status from scratch")
        _, short_height, _ = self._build_fork(0, 1, rewind=7)
        competing_tip = self.nodes[1].getbestblockhash()
        self._connect_expect_parked(0, 1, short_height, competing_tip)
        self.disconnect_nodes(0, 1)
        # Full reindex discards the index and active chainstate. Unpruned import
        # initially activates only genesis, so there is no deep active rewind.
        self.restart_node(0, extra_args=["-parkdeepreorg=1", "-reindex"])
        assert_equal(self.nodes[0].getbestblockhash(), competing_tip)
        assert all(not tip["parked"] for tip in self.nodes[0].getchaintips())
        self.connect_nodes(0, 1)
        self.sync_all()

    def test_parking_disabled(self):
        self.log.info("parkdeepreorg=0: a deep competing chain is not parked")
        # Node 1 has -parkdeepreorg=0. Give it the shorter chain so it would
        # rewind more than the default depth if parking were enabled.
        _, short_height, long_height = self._build_fork(1, 0, rewind=7)
        self._connect_expect_follow(1, 0, long_height)
        assert_equal(self.nodes[1].getblockcount(), long_height)
        assert short_height < long_height
        assert all(not tip["parked"] for tip in self.nodes[1].getchaintips())

    def run_test(self):
        self.test_invalid_parkreorgdepth()
        self.generate(self.nodes[0], 10, sync_fun=self.sync_all)
        assert_equal(self.nodes[0].getblockcount(), 10)
        self.test_default_boundary()
        self.test_custom_threshold()
        self.test_headers_first_missing_ancestor_last()
        self.test_null_pblock_reconsider()
        self.test_out_of_order_import()
        self.test_reindex_rebuild()
        self.test_parking_disabled()


if __name__ == "__main__":
    ParkDeepReorgTest(__file__).main()
