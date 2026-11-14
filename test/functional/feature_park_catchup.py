#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Deep-reorg parking only protects blocks connected after catch-up.

Blocks connected while a node is catching up (after every start, in IBD) are
not protected, so a short sibling extension received first cannot make a
lagging node park the longer chain. Blocks connected after catch-up ended
remain protected, including across a restart, and parking raises a warning.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

PARK_ARGS = ["-parkdeepreorg=1", "-parkcatchuppeers=2", "-checkblockindex=1"]


class ParkCatchupTest(BitcoinTestFramework):
    def set_test_params(self):
        # 0: parking node, 1 and 2: peers on the main chain, 3: competitor.
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.extra_args = [PARK_ARGS, [], [], []]

    def setup_network(self):
        self.setup_nodes()

    def caught_up(self, node):
        return not node.getblockchaininfo()["parkcatchup"]

    def tip_parked(self, node, tip_hash):
        return next(t for t in node.getchaintips() if t["hash"] == tip_hash)["parked"]

    def parked_warning(self, node):
        return any("parked" in w for w in node.getblockchaininfo()["warnings"])

    def isolate(self, idx):
        for peer in self.nodes[idx].getpeerinfo():
            self.nodes[idx].disconnectnode(nodeid=peer["id"])
        self.wait_until(lambda: not self.nodes[idx].getpeerinfo())

    def run_test(self):
        honest = self.nodes[1]
        self.connect_nodes(2, 1)
        self.connect_nodes(3, 1)
        self.generate(honest, 10, sync_fun=lambda: self.sync_blocks(self.nodes[1:]))

        self.test_lagging_node_recovers()
        self.test_synced_node_parks_with_warning()
        self.test_restart_keeps_protection()

    def test_lagging_node_recovers(self):
        self.log.info("A restarted node extended by a short sibling chain still follows the longer chain")
        node, honest, attacker, competitor = self.nodes
        self.connect_nodes(0, 1)
        self.connect_nodes(0, 2)
        self.wait_until(lambda: self.caught_up(node))
        self.generate(honest, 2, sync_fun=lambda: self.sync_blocks(self.nodes[:3]))
        fork_height = node.getblockcount()

        for i in (0, 1, 2):
            self.isolate(i)
        self.stop_node(0)
        self.generate(honest, 30, sync_fun=self.no_op)
        self.generate(attacker, 4, sync_fun=self.no_op)
        honest_tip = honest.getbestblockhash()
        attacker_tip = attacker.getbestblockhash()
        assert_equal(attacker.getblockcount(), fork_height + 4)

        self.start_node(0, extra_args=PARK_ARGS)
        assert node.getblockchaininfo()["parkcatchup"]
        self.connect_nodes(0, 2)
        self.wait_until(lambda: node.getbestblockhash() == attacker_tip)
        # One agreeing peer is fewer than -parkcatchuppeers.
        assert node.getblockchaininfo()["parkcatchup"]

        with node.assert_debug_log([], unexpected_msgs=["Parking block"]):
            self.connect_nodes(0, 1)
            self.wait_until(lambda: node.getbestblockhash() == honest_tip)
        assert not self.tip_parked(node, honest_tip)
        self.sync_blocks(self.nodes[:3])
        self.wait_until(lambda: self.caught_up(node))
        assert not self.parked_warning(node)

    def test_synced_node_parks_with_warning(self):
        self.log.info("After catch-up, rewinding more than 3 protected blocks parks and warns")
        node, honest, attacker, competitor = self.nodes
        self.connect_nodes(3, 1)
        self.sync_blocks()
        self.isolate(3)
        self.generate(honest, 5, sync_fun=lambda: self.sync_blocks(self.nodes[:3]))
        self.generate(competitor, 7, sync_fun=self.no_op)
        competing_tip = competitor.getbestblockhash()
        competing_root = competitor.getblockhash(node.getblockcount() - 4)
        original_tip = node.getbestblockhash()

        with node.assert_debug_log(["Parking block"], timeout=30):
            self.connect_nodes(0, 3)
        self.wait_until(lambda: any(t["hash"] == competing_tip for t in node.getchaintips()))
        self.wait_until(lambda: self.parked_warning(node))
        assert_equal(node.getbestblockhash(), original_tip)
        assert self.tip_parked(node, competing_tip)

        node.unparkblock(competing_root)
        self.wait_until(lambda: node.getbestblockhash() == competing_tip)
        assert not self.parked_warning(node)

    def test_restart_keeps_protection(self):
        self.log.info("Blocks protected before a restart stay protected while catching up")
        node, honest, attacker, competitor = self.nodes
        self.sync_blocks()
        self.isolate(3)
        self.generate(honest, 5, sync_fun=lambda: self.sync_blocks(self.nodes[:3]))
        self.generate(competitor, 7, sync_fun=self.no_op)
        competing_tip = competitor.getbestblockhash()
        original_tip = node.getbestblockhash()

        self.isolate(0)
        self.restart_node(0, extra_args=PARK_ARGS)
        assert node.getblockchaininfo()["parkcatchup"]
        with node.assert_debug_log(["Parking block"], timeout=30):
            self.connect_nodes(0, 3)
        self.wait_until(lambda: any(t["hash"] == competing_tip for t in node.getchaintips()))
        assert_equal(node.getbestblockhash(), original_tip)
        assert self.tip_parked(node, competing_tip)
        assert node.getblockchaininfo()["parkcatchup"]


if __name__ == "__main__":
    ParkCatchupTest(__file__).main()
