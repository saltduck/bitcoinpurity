#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license.
"""Test Purity relay and mining fee defaults without the Core policy profile."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.wallet import MiniWallet


class PolicyDefaultsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [["-corepolicy=0", "-blockprioritysize=0"]]

    def run_test(self):
        node = self.nodes[0]
        info = node.getmempoolinfo()
        assert_equal(info["minrelaytxfee"], Decimal("0.000001"))
        assert_equal(info["incrementalrelayfee"], Decimal("0.0000001"))

        wallet = MiniWallet(node)
        self.generate(wallet, 102)
        below_relay = wallet.create_self_transfer(fee_rate=Decimal("0.0000008"))
        assert_equal(node.testmempoolaccept([below_relay["hex"]])[0]["allowed"], False)
        below_mining = wallet.send_self_transfer(from_node=node, fee_rate=Decimal("0.000002"))
        above_mining = wallet.send_self_transfer(from_node=node, fee_rate=Decimal("0.000005"))
        template = node.getblocktemplate({"rules": ["segwit"]})
        assert_equal([tx["txid"] for tx in template["transactions"]], [above_mining["txid"]])
        self.generate(node, 1)
        assert_equal(node.getrawmempool(), [below_mining["txid"]])

        utxo = wallet.get_utxo(confirmed_only=True)
        original = wallet.send_self_transfer(from_node=node, utxo_to_spend=utxo, fee=Decimal("0.00001"))
        replacement = wallet.send_self_transfer(from_node=node, utxo_to_spend=utxo, fee=Decimal("0.0000101"))
        assert original["txid"] not in node.getrawmempool()
        assert replacement["txid"] in node.getrawmempool()

        self.restart_node(0, extra_args=["-corepolicy=0", "-minrelaytxfee=0.000003", "-incrementalrelayfee=0.0000002"])
        info = node.getmempoolinfo()
        assert_equal(info["minrelaytxfee"], Decimal("0.000003"))
        assert_equal(info["incrementalrelayfee"], Decimal("0.0000002"))


if __name__ == "__main__":
    PolicyDefaultsTest(__file__).main()
