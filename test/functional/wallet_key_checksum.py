#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Check persistent key access and signing before and after wallet encryption."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class WalletKeyChecksumTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [["-keypool=2"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        wallet = node.get_wallet_rpc(self.default_wallet_name)
        address = wallet.getnewaddress(address_type="legacy")
        address_info = wallet.getaddressinfo(address)
        descriptors = [d["desc"] for d in wallet.listdescriptors()["descriptors"]] if self.options.descriptors else None
        message = "wallet key checksum compatibility"
        assert node.verifymessage(address, wallet.signmessage(address, message), message)

        wallet.unloadwallet()
        node.loadwallet(self.default_wallet_name)
        assert_equal(wallet.getwalletinfo()["format"], "sqlite" if self.options.descriptors else "bdb")
        assert_equal(wallet.getaddressinfo(address)["pubkey"], address_info["pubkey"])
        if descriptors is not None:
            assert_equal([d["desc"] for d in wallet.listdescriptors()["descriptors"]], descriptors)
        assert node.verifymessage(address, wallet.signmessage(address, message), message)

        passphrase = "checksum-test-passphrase"
        wallet.encryptwallet(passphrase)
        wallet.unloadwallet()
        node.loadwallet(self.default_wallet_name)
        assert_raises_rpc_error(-13, "Please enter the wallet passphrase", wallet.signmessage, address, message)
        wallet.walletpassphrase(passphrase, 60)
        assert node.verifymessage(address, wallet.signmessage(address, message), message)
        wallet.walletlock()
        assert_raises_rpc_error(-13, "Please enter the wallet passphrase", wallet.signmessage, address, message)

        self.restart_node(0)
        wallet = node.get_wallet_rpc(self.default_wallet_name)
        assert_raises_rpc_error(-13, "Please enter the wallet passphrase", wallet.signmessage, address, message)
        wallet.walletpassphrase(passphrase, 60)
        assert node.verifymessage(address, wallet.signmessage(address, message), message)
        wallet.walletlock()


if __name__ == "__main__":
    WalletKeyChecksumTest(__file__).main()
