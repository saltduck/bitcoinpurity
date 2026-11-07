#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Shared-magic history, forged services and full-block peer evidence."""

import copy
import json
from pathlib import Path

from feature_purity_signet import PuritySignetTest
from feature_signet import signet_blocks
from test_framework.messages import CBlock, NODE_NETWORK, NODE_WITNESS, NODE_REDUCED_DATA, NODE_PURITY_ASERT, from_hex, msg_block
from test_framework.p2p import P2PDataStore
from test_framework.util import assert_equal


class PuritySignetP2PTest(PuritySignetTest):
    def set_test_params(self):
        super().set_test_params()
        self.num_nodes = 2
        self.extra_args *= 2

    def setup_network(self):
        self.setup_nodes()

    def run_test(self):
        node, download = self.nodes
        history = []
        for raw in signet_blocks:
            assert_equal(node.submitblock(raw), None)
            block = from_hex(CBlock(), raw)
            block.rehash()
            history.append(block)
        good = self.block()
        alternate = self.block(signers=(43, 44))
        assert alternate.hash != good.hash
        params = copy.deepcopy(self.params)
        params["activation_hash"] = good.hash
        pinned_file = Path(self.options.tmpdir) / "pinned.json"
        pinned_file.write_text(json.dumps(params))
        for index in range(2):
            self.restart_node(index, extra_args=[f"-signetparams={pinned_file}", "-checkblockindex=1"])
        assert_equal(node.submitblock(good.serialize().hex()), None)
        services = NODE_NETWORK | NODE_WITNESS | NODE_REDUCED_DATA | NODE_PURITY_ASERT

        self.log.info("Service flags alone do not verify Signet chain membership")
        stale = P2PDataStore()
        for block in [*history, alternate]:
            stale.block_store[block.sha256] = block
        stale.last_block_hash = alternate.sha256
        with download.assert_debug_log(["retaining shared history for download"]):
            download.add_outbound_p2p_connection(stale, p2p_idx=0, connection_type="outbound-full-relay", services=services, wait_for_disconnect=True)
            self.wait_until(lambda: download.getblockcount() == 10)
        stale.wait_for_disconnect()
        assert_equal(download.getblockchaininfo()["headers"], 10)
        assert_equal(download.listbanned(), [])

        self.log.info("A valid full Purity block qualifies the serving peer")
        peer = P2PDataStore()
        peer.block_store = {b.sha256: b for b in [*history, good]}
        peer.last_block_hash = good.sha256
        download.add_outbound_p2p_connection(peer, p2p_idx=1, connection_type="outbound-full-relay", services=services)
        self.wait_until(lambda: download.getbestblockhash() == good.hash)
        self.wait_until(lambda: any(p["signet_chain_verified"] for p in download.getpeerinfo()))
        assert_equal(download.listbanned(), [])

        self.log.info("A duplicate known header with corrupted witness cannot qualify a new peer")
        before = {p["id"] for p in download.getpeerinfo()}
        corrupt_peer = P2PDataStore()
        download.add_outbound_p2p_connection(corrupt_peer, p2p_idx=2, connection_type="outbound-full-relay", services=services)
        malformed = copy.deepcopy(good)
        malformed.vtx[0].wit.vtxinwit[0].scriptWitness.stack[0] = bytes([1]) * 32
        with download.assert_debug_log(["Received mutated block"]):
            corrupt_peer.send_message(msg_block(malformed))
            corrupt_peer.wait_for_disconnect()
        assert_equal({p["id"] for p in download.getpeerinfo()}, before)


if __name__ == "__main__":
    PuritySignetP2PTest(__file__).main()
