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

    def setup_chain(self):
        self.preparing_file = Path(self.options.tmpdir) / "miner-preparing.json"
        preparing = dict(self.params, schema_version=2)
        for field in ("activation_height", "parent_hash", "anchor", "activation_hash"):
            preparing[field] = None
        self.preparing_file.write_text(json.dumps(preparing))
        self.extra_args[0] = [f"-signetparams={self.preparing_file}", "-checkblockindex=1", "-blocksxor=0", "-mocktime=1598920800"]
        super().setup_chain()

    def setup_network(self):
        self.setup_nodes()
        for index in range(1, self.num_nodes):
            self.connect_nodes(0, index)

    def skip_test_if_missing_module(self):
        super().skip_test_if_missing_module()
        self.skip_if_no_cli()
        self.skip_if_no_wallet()
        self.skip_if_no_sqlite()

    def advance_mining_time(self, timestamp):
        self.restart_node(0, extra_args=self.extra_args[0] + [f"-mocktime={timestamp}"])
        for index in range(1, self.num_nodes):
            if self.nodes[index].running:
                self.connect_nodes(0, index)

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
        self.log.info("Freeze the prepared mining node after both signing wallets are ready")
        frozen = Path(self.options.tmpdir) / "miner-frozen.json"
        root = Path(self.config["environment"]["SRCDIR"])
        freeze_cli = [node.cli.binary, f"-datadir={node.cli.datadir}", "-signet", f"-signetparams={self.preparing_file}"]
        result = subprocess.run([sys.executable, str(root / "contrib/purity-signet/prepare.py"),
                                 "--config", str(self.preparing_file), "--output", str(frozen),
                                 "--node-cli", json.dumps(freeze_cli), "--ready", "--max-parent-age", "1000000000"], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        node.wait_until_stopped()
        self.params_file = frozen
        self.extra_args[0] = [f"-signetparams={frozen}", "-checkblockindex=1", "-blocksxor=0"]
        self.start_node(0, extra_args=self.extra_args[0])
        for index in range(1, self.num_nodes):
            self.connect_nodes(0, index)
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
        assert_equal(json.loads(result.stdout)["hash"], first_hash)
        for status in ("Signing request", "signer=1/2", "signer=2/2", "Signature quorum complete",
                       "Checking signed proposal", "PoW started", "PoW complete", "Submitting block", "Block accepted"):
            assert status in result.stderr, result.stderr
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
                assert "retrying in 1s" in stderr_path.read_text()
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

        self.log.info("Three configured signers tolerate any single daemon outage without changing the config")
        config["signer_clis"] = [clis[i + 1] + [f"-rpcwallet=authority-{i}"] for i in range(3)]
        config["signer_timeout_seconds"] = 3
        config_path.write_text(json.dumps(config))
        for missing, pair in ((1, "2+3"), (2, "1+3"), (3, "1+2")):
            self.stop_node(missing)
            previous = node.getblockheader(node.getbestblockhash())
            self.advance_mining_time(previous["time"] + 600)
            expected_bits = node.getblocktemplate({"rules": ["segwit", "signet"]})["bits"]
            result = subprocess.run([sys.executable, str(miner), "--config", str(config_path), "--once"],
                                    capture_output=True, text=True, timeout=90)
            assert result.returncode == 0, result.stderr
            assert_equal(json.loads(result.stdout)["height"], previous["height"] + 1)
            assert_equal(json.loads(result.stdout)["bits"], expected_bits)
            assert "signers=" + pair in result.stderr, result.stderr
            self.start_node(missing, extra_args=self.extra_args[missing])
            self.connect_nodes(0, missing)
            self.sync_blocks()

        self.log.info("A locked authority wallet is bypassed without weakening signature checks")
        wallets[0] = self.nodes[1].get_wallet_rpc("authority-0")
        wallets[0].encryptwallet("test-signer-failover")
        previous = node.getblockheader(node.getbestblockhash())
        self.advance_mining_time(previous["time"] + 600)
        try:
            result = subprocess.run([sys.executable, str(miner), "--config", str(config_path), "--once"],
                                    capture_output=True, text=True, timeout=90)
            assert result.returncode == 0, result.stderr
            assert "Signing request failed" in result.stderr, result.stderr
            assert "signers=2+3" in result.stderr, result.stderr
            assert_equal(json.loads(result.stdout)["height"], previous["height"] + 1)
        finally:
            wallets[0].walletpassphrase("test-signer-failover", 3600)
        self.sync_blocks()

        self.log.info("A wallet without an authority key cannot count as a quorum member")
        self.nodes[1].createwallet("no-authority", disable_private_keys=True, descriptors=True)
        config["signer_clis"][0] = clis[1] + ["-rpcwallet=no-authority"]
        config_path.write_text(json.dumps(config))
        previous = node.getblockheader(node.getbestblockhash())
        self.advance_mining_time(previous["time"] + 600)
        result = subprocess.run([sys.executable, str(miner), "--config", str(config_path), "--once"],
                                capture_output=True, text=True, timeout=90)
        assert result.returncode == 0, result.stderr
        assert "signers=2+3" in result.stderr, result.stderr
        assert_equal(json.loads(result.stdout)["height"], previous["height"] + 1)
        self.sync_blocks()

        self.log.info("Two distinct endpoints holding the same key still need the third authority")
        config["signer_clis"] = [clis[1] + ["-rpcwallet=authority-0"],
                                 clis[2] + ["-rpcwallet=recovered-a"], clis[3] + ["-rpcwallet=authority-2"]]
        config_path.write_text(json.dumps(config))
        previous = node.getblockheader(node.getbestblockhash())
        self.advance_mining_time(previous["time"] + 600)
        result = subprocess.run([sys.executable, str(miner), "--config", str(config_path), "--once"],
                                capture_output=True, text=True, timeout=90)
        assert result.returncode == 0, result.stderr
        assert "signers=1+3" in result.stderr, result.stderr
        assert_equal(json.loads(result.stdout)["height"], previous["height"] + 1)
        self.sync_blocks()

        self.log.info("Continuous three-endpoint mining pauses below quorum and resumes on recovery")
        config["signer_clis"] = [clis[i + 1] + [f"-rpcwallet=authority-{i}"] for i in range(3)]
        config_path.write_text(json.dumps(config))
        self.stop_node(1)
        self.stop_node(2)
        previous = node.getblockheader(node.getbestblockhash())
        self.advance_mining_time(previous["time"] + 600)
        with stdout_path.open("w") as stdout, stderr_path.open("w") as stderr:
            process = subprocess.Popen([sys.executable, str(miner), "--config", str(config_path)], stdout=stdout, stderr=stderr)
            try:
                self.wait_until(lambda: "retrying in 1s" in stderr_path.read_text())
                assert_equal(node.getblockcount(), previous["height"])
                assert "PoW started" not in stderr_path.read_text()
                assert "Submitting block" not in stderr_path.read_text()
                self.start_node(2, extra_args=self.extra_args[2])
                self.connect_nodes(0, 2)
                self.wait_until(lambda: node.getblockcount() == previous["height"] + 1, timeout=90)
                self.wait_until(lambda: stdout_path.read_text().endswith("\n"))
            finally:
                process.terminate()
                process.wait(timeout=30)
        assert_equal(len(stdout_path.read_text().splitlines()), 1)
        assert "signers=2+3" in stderr_path.read_text()
        self.start_node(1, extra_args=self.extra_args[1])
        self.connect_nodes(0, 1)
        self.sync_blocks()
        assert_equal(node.verifychain(4, 0), True)


if __name__ == "__main__":
    PuritySignetMinerTest(__file__).main()
