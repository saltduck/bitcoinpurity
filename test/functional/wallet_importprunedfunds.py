#!/usr/bin/env python3
# Copyright (c) 2014-2022 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the importprunedfunds and removeprunedfunds RPCs."""
from decimal import Decimal

from test_framework.address import key_to_p2wpkh
from test_framework.blocktools import COINBASE_MATURITY
from test_framework.messages import (
    CMerkleBlock,
    from_hex,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)
from test_framework.wallet_util import generate_keypair


class ImportPrunedFundsTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Mining blocks...")
        self.generate(self.nodes[0], COINBASE_MATURITY + 1)

        # address
        address1 = self.nodes[0].getnewaddress()
        # pubkey
        address2 = self.nodes[0].getnewaddress()
        # privkey
        address3_privkey, address3_pubkey = generate_keypair(wif=True)
        address3 = key_to_p2wpkh(address3_pubkey)
        self.nodes[0].importprivkey(address3_privkey)

        # Check only one address
        address_info = self.nodes[0].getaddressinfo(address1)
        assert_equal(address_info['ismine'], True)

        self.sync_all()

        # Node 1 sync test
        assert_equal(self.nodes[1].getblockcount(), COINBASE_MATURITY + 1)

        # Address Test - before import
        address_info = self.nodes[1].getaddressinfo(address1)
        assert_equal(address_info['iswatchonly'], False)
        assert_equal(address_info['ismine'], False)

        address_info = self.nodes[1].getaddressinfo(address2)
        assert_equal(address_info['iswatchonly'], False)
        assert_equal(address_info['ismine'], False)

        address_info = self.nodes[1].getaddressinfo(address3)
        assert_equal(address_info['iswatchonly'], False)
        assert_equal(address_info['ismine'], False)

        # Send funds to self
        txnid1 = self.nodes[0].sendtoaddress(address1, 0.1)
        self.generate(self.nodes[0], 1)
        rawtxn1 = self.nodes[0].gettransaction(txnid1)['hex']
        proof1 = self.nodes[0].gettxoutproof([txnid1])

        txnid2 = self.nodes[0].sendtoaddress(address2, 0.05)
        self.generate(self.nodes[0], 1)
        rawtxn2 = self.nodes[0].gettransaction(txnid2)['hex']
        proof2 = self.nodes[0].gettxoutproof([txnid2])

        txnid3 = self.nodes[0].sendtoaddress(address3, 0.025)
        self.generate(self.nodes[0], 1)
        rawtxn3 = self.nodes[0].gettransaction(txnid3)['hex']
        proof3 = self.nodes[0].gettxoutproof([txnid3])

        self.sync_all()

        # Import with no affiliated address
        assert_raises_rpc_error(-5, "No addresses", self.nodes[1].importprunedfunds, rawtxn1, proof1)

        balance1 = self.nodes[1].getbalance()
        assert_equal(balance1, Decimal(0))

        # Import with affiliated address with no rescan
        self.nodes[1].createwallet('wwatch', disable_private_keys=True)
        wwatch = self.nodes[1].get_wallet_rpc('wwatch')
        wwatch.importaddress(address=address2, rescan=False)
        wwatch.importprunedfunds(rawtransaction=rawtxn2, txoutproof=proof2)
        assert [tx for tx in wwatch.listtransactions(include_watchonly=True) if tx['txid'] == txnid2]

        # Import with private key with no rescan
        w1 = self.nodes[1].get_wallet_rpc(self.default_wallet_name)
        w1.importprivkey(privkey=address3_privkey, rescan=False)
        w1.importprunedfunds(rawtxn3, proof3)
        assert [tx for tx in w1.listtransactions() if tx['txid'] == txnid3]
        balance3 = w1.getbalance()
        assert_equal(balance3, Decimal('0.025'))

        # Addresses Test - after import
        address_info = w1.getaddressinfo(address1)
        assert_equal(address_info['iswatchonly'], False)
        assert_equal(address_info['ismine'], False)
        address_info = wwatch.getaddressinfo(address2)
        if self.options.descriptors:
            assert_equal(address_info['iswatchonly'], False)
            assert_equal(address_info['ismine'], True)
        else:
            assert_equal(address_info['iswatchonly'], True)
            assert_equal(address_info['ismine'], False)
        address_info = w1.getaddressinfo(address3)
        assert_equal(address_info['iswatchonly'], False)
        assert_equal(address_info['ismine'], True)

        # Remove transactions
        assert_raises_rpc_error(-4, f'Transaction {txnid1} does not belong to this wallet', w1.removeprunedfunds, txnid1)
        assert not [tx for tx in w1.listtransactions(include_watchonly=True) if tx['txid'] == txnid1]

        wwatch.removeprunedfunds(txnid2)
        assert not [tx for tx in wwatch.listtransactions(include_watchonly=True) if tx['txid'] == txnid2]

        w1.removeprunedfunds(txnid3)
        assert not [tx for tx in w1.listtransactions(include_watchonly=True) if tx['txid'] == txnid3]

        # Check various RPC parameter validation errors
        assert_raises_rpc_error(-22, "TX decode failed", w1.importprunedfunds, b'invalid tx'.hex(), proof1)
        assert_raises_rpc_error(-5, "Transaction given doesn't exist in proof", w1.importprunedfunds, rawtxn2, proof1)

        mb = from_hex(CMerkleBlock(), proof1)
        mb.header.hashMerkleRoot = 0xdeadbeef  # cause mismatch between merkle root and merkle block
        assert_raises_rpc_error(-5, "Something wrong with merkleblock", w1.importprunedfunds, rawtxn1, mb.serialize().hex())

        mb = from_hex(CMerkleBlock(), proof1)
        mb.header.nTime += 1  # modify arbitrary block header field to change block hash
        assert_raises_rpc_error(-5, "Block not found in chain", w1.importprunedfunds, rawtxn1, mb.serialize().hex())

        self.log.info("Test removeprunedfunds with conflicting transactions")
        node = self.nodes[0]
        funding_wallet = node.get_wallet_rpc(self.default_wallet_name)
        node.createwallet("conflicting", descriptors=self.options.descriptors)
        wallet = node.get_wallet_rpc("conflicting")
        funding_wallet.sendmany("", {wallet.getnewaddress(): 10 for _ in range(3)})
        self.generate(node, 1)

        for num_inputs in (1, 2):
            utxos = wallet.listunspent()[:num_inputs]
            assert_equal(len(utxos), num_inputs)
            inputs = [{"txid": u["txid"], "vout": u["vout"]} for u in utxos]
            balance = wallet.getbalance()
            addr = wallet.getnewaddress()
            tx1_id = wallet.send(outputs=[{addr: 1}], fee_rate=10,
                                 options={"inputs": inputs, "add_inputs": False, "replaceable": True})["txid"]
            tx1_fee = wallet.gettransaction(tx1_id)["fee"]

            # Replace tx1 with a higher-fee transaction spending the same inputs.
            output_value = sum(u["amount"] for u in utxos) + tx1_fee - Decimal("0.0001")
            raw_tx2 = wallet.createrawtransaction(inputs, [{addr: output_value}], replaceable=True)
            signed_tx2 = wallet.signrawtransactionwithwallet(raw_tx2)
            assert_equal(signed_tx2["complete"], True)
            tx2_id = node.sendrawtransaction(signed_tx2["hex"])
            assert tx2_id != tx1_id
            assert_equal(wallet.gettransaction(tx1_id)["walletconflicts"], [tx2_id])
            tx2 = wallet.gettransaction(tx2_id)
            assert_equal(tx2["walletconflicts"], [tx1_id])
            assert_equal(tx2["confirmations"], 0)
            assert tx1_id not in node.getrawmempool()
            assert tx2_id in node.getrawmempool()

            # A failed deletion must leave the wallet and spend records unchanged.
            missing_txid = "00" * 32
            available_before = wallet.listunspent(minconf=0)
            balances_before = wallet.getbalances()
            assert_raises_rpc_error(-4, f"Transaction {missing_txid} does not belong to this wallet",
                                    wallet.removeprunedfunds, missing_txid)
            assert_equal(wallet.listunspent(minconf=0), available_before)
            assert_equal(wallet.getbalances(), balances_before)
            assert_equal(wallet.gettransaction(tx1_id)["txid"], tx1_id)
            assert_equal(wallet.gettransaction(tx2_id)["txid"], tx2_id)

            wallet.removeprunedfunds(tx1_id)
            assert_raises_rpc_error(-5, "Invalid or non-wallet transaction id", wallet.gettransaction, tx1_id)
            assert_equal(wallet.gettransaction(tx2_id)["txid"], tx2_id)
            available = {(u["txid"], u["vout"]) for u in wallet.listunspent(minconf=0)}
            for u in utxos:
                assert (u["txid"], u["vout"]) not in available, "UTXO should still be spent by conflicting tx"
                assert_equal(node.gettxout(u["txid"], u["vout"]), None)

            self.generate(node, 1)
            assert_equal(wallet.gettransaction(tx2_id)["confirmations"], 1)
            assert_equal(wallet.getbalance(), balance + tx2["fee"])
            available = {(u["txid"], u["vout"]) for u in wallet.listunspent(minconf=0)}
            for u in utxos:
                assert (u["txid"], u["vout"]) not in available

            # Coin selection must still construct a valid spend after confirmation.
            funded = wallet.fundrawtransaction(wallet.createrawtransaction([], {funding_wallet.getnewaddress(): 1}))
            signed = wallet.signrawtransactionwithwallet(funded["hex"])
            assert_equal(signed["complete"], True)
            assert_equal(node.testmempoolaccept([signed["hex"]])[0]["allowed"], True)

            node.unloadwallet("conflicting")
            node.loadwallet("conflicting")
            wallet = node.get_wallet_rpc("conflicting")
            assert_raises_rpc_error(-5, "Invalid or non-wallet transaction id", wallet.gettransaction, tx1_id)
            assert_equal(wallet.gettransaction(tx2_id)["confirmations"], 1)
            assert_equal(wallet.getbalance(), balance + tx2["fee"])
            available = {(u["txid"], u["vout"]) for u in wallet.listunspent(minconf=0)}
            for u in utxos:
                assert (u["txid"], u["vout"]) not in available

        # Removing the sole remaining spend must release its input associations.
        wallet.removeprunedfunds(tx2_id)
        assert_raises_rpc_error(-5, "Invalid or non-wallet transaction id", wallet.gettransaction, tx2_id)
        available = {(u["txid"], u["vout"]) for u in wallet.listunspent(minconf=0)}
        for u in utxos:
            assert (u["txid"], u["vout"]) in available


if __name__ == '__main__':
    ImportPrunedFundsTest(__file__).main()
