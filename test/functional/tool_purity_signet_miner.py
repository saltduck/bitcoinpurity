#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Independent wallet signatures, exact template work and miner restart."""

import json
from pathlib import Path
import subprocess
import sys
import time

from feature_purity_signet import PuritySignetTest
from feature_signet import signet_blocks
from test_framework.descriptors import descsum_create
from test_framework.util import assert_equal
from test_framework.wallet_util import bytes_to_wif


class PuritySignetMinerTest(PuritySignetTest):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True, legacy=False)
        parser.add_argument("--real-cadence", action="store_true", help="Wait for the real 600-second production interval")

    def set_test_params(self):
        super().set_test_params()
        self.num_nodes = 4
        self.extra_args = [self.extra_args[0].copy() for _ in range(self.num_nodes)]

    def setup_network(self):
        self.setup_nodes()
        for index in range(1, self.num_nodes):
            self.connect_nodes(0, index)

    def skip_test_if_missing_module(self):
        super().skip_test_if_missing_module()
        self.skip_if_no_cli()
        self.skip_if_no_wallet()
        self.skip_if_no_sqlite()

    def run_test(self):
        node = self.nodes[0]
        for raw in signet_blocks:
            assert_equal(node.submitblock(raw), None)
        wallets = []
        for signer in range(3):
            name = f"authority-{signer}"
            signer_node = self.nodes[signer + 1]
            signer_node.createwallet(name, descriptors=True, load_on_startup=True)
            wallet = signer_node.get_wallet_rpc(name)
            keys = self.params["public_keys"].copy()
            keys[signer] = bytes_to_wif((42 + signer).to_bytes(32, "big"))
            descriptor = descsum_create(f"multi(2,{','.join(keys)})")
            result = wallet.importdescriptors([{"desc": descriptor, "timestamp": "now"}])
            assert_equal(result[0]["success"], True)
            wallets.append(wallet)
        payout = wallets[0].getaddressinfo(wallets[0].getnewaddress())["scriptPubKey"]
        clis = [[n.cli.binary, f"-datadir={n.cli.datadir}", "-signet", f"-signetparams={self.params_file}"] for n in self.nodes]
        miner = Path(self.config["environment"]["SRCDIR"]) / "contrib/purity-signet/miner.py"
        config_path = Path(self.options.tmpdir) / "miner.json"
        config = {"schema_version": 1, "chain_params": str(self.params_file), "node_cli": clis[0],
                  "signer_clis": [clis[1] + ["-rpcwallet=authority-0"], clis[2] + ["-rpcwallet=authority-1"]],
                  "bitcoin_util": self.options.bitcoinutil, "reward_script": payout, "poll_seconds": 1}
        config_path.write_text(json.dumps(config))
        self.log.info("A standalone miner uses two wallets and exact GBT difficulty")
        expected_bits = node.getblocktemplate({"rules": ["segwit", "signet"]})["bits"]
        result = subprocess.run([sys.executable, str(miner), "--config", str(config_path), "--once"], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        first_hash = node.getbestblockhash()
        first = node.getblockheader(first_hash)
        assert_equal(first["height"], 11)
        assert_equal(first["bits"], expected_bits)
        self.sync_blocks()
        backup = Path(self.options.tmpdir) / "authority-a-backup.dat"
        wallets[0].backupwallet(str(backup))

        self.log.info("Continuous miner survives a signer outage without producing an unsigned block")
        self.stop_node(2)
        if not self.options.real_cadence:
            self.restart_node(0, extra_args=self.extra_args[0] + [f"-mocktime={max(int(time.time()), first['time']) + 600}"])
            self.connect_nodes(0, 1)
            self.connect_nodes(0, 3)
        stdout_path = Path(self.options.tmpdir) / "continuous-miner.stdout"
        stderr_path = Path(self.options.tmpdir) / "continuous-miner.stderr"
        with stdout_path.open("w") as stdout, stderr_path.open("w") as stderr:
            process = subprocess.Popen([sys.executable, str(miner), "--config", str(config_path)], stdout=stdout, stderr=stderr)
            try:
                self.wait_until(lambda: "RPC walletprocesspsbt failed" in stderr_path.read_text(), timeout=720 if self.options.real_cadence else 60)
                assert process.poll() is None
                assert_equal(node.getblockcount(), 11)
                self.start_node(2, extra_args=self.extra_args[2])
                self.connect_nodes(0, 2)
                self.wait_until(lambda: node.getblockcount() == 12, timeout=90)
                self.wait_until(lambda: stdout_path.read_text().endswith("\n"))
            finally:
                process.terminate()
                process.wait(timeout=30)
        records = [json.loads(line) for line in stdout_path.read_text().splitlines()]
        assert_equal(len(records), 1)
        assert_equal(records[0]["height"], 12)
        second = node.getblockheader(node.getbestblockhash())
        assert_equal(second["height"], 12)
        assert_equal(second["bits"], records[0]["bits"])
        assert second["time"] >= first["time"] + 600
        self.sync_blocks()

        self.log.info("A backup restored on another daemon signs with the surviving authority")
        restored = self.nodes[2].restorewallet("recovered-a", str(backup))
        assert_equal(restored["name"], "recovered-a")
        config["signer_clis"] = [clis[2] + ["-rpcwallet=recovered-a"], clis[3] + ["-rpcwallet=authority-2"]]
        config_path.write_text(json.dumps(config))
        if self.options.real_cadence:
            # Backup/quorum recovery is exercised by the fast run; do not add a
            # second ten-minute wait to the real interval measurement.
            assert_equal(node.verifychain(4, 0), True)
            return
        self.restart_node(0, extra_args=self.extra_args[0] + [f"-mocktime={second['time'] + 600}"])
        for index in range(1, self.num_nodes):
            self.connect_nodes(0, index)
        expected_bits = node.getblocktemplate({"rules": ["segwit", "signet"]})["bits"]
        result = subprocess.run([sys.executable, str(miner), "--config", str(config_path), "--once"], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        third = node.getblockheader(node.getbestblockhash())
        assert_equal(third["height"], 13)
        assert_equal(third["bits"], expected_bits)
        assert third["time"] >= second["time"] + 600
        self.sync_blocks()
        assert_equal(node.verifychain(4, 0), True)


if __name__ == "__main__":
    PuritySignetMinerTest(__file__).main()
