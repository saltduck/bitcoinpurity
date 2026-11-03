#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license.
"""Exercise the embedded V1 -> authenticated SV2/JDS -> Purity validation path."""
import json
import os
from pathlib import Path
import socket
import subprocess
import time
from feature_datum import StratumClient
from test_framework.messages import hash256, uint256_from_compact, CBlock
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_node import ErrorMatch
from test_framework.util import assert_equal, p2p_port
from test_framework.wallet import MiniWallet
from test_framework.messages import from_hex
from test_framework.test_framework import SkipTest


class Sv2Test(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

    def add_options(self, parser):
        parser.add_argument("--extranonce-size", type=int, choices=(12, 16, 32), default=16)
        parser.add_argument("--coinbase-tags", action="store_true")
        parser.add_argument("--job-interval", type=int, choices=(30, 60), default=30)
        parser.add_argument("--sv2-test-pool", default=os.getenv("SV2_TEST_POOL"))

    def skip_test_if_missing_module(self):
        if not self.config["components"].getboolean("ENABLE_SV2", fallback=False):
            raise SkipTest("SV2 support not compiled")
        if not self.options.sv2_test_pool:
            self.options.sv2_test_pool = str(Path(self.config["environment"]["BUILDDIR"]) / "bin" /
                                           ("purity-sv2-test-pool" + self.config["environment"]["EXEEXT"]))
        if not Path(self.options.sv2_test_pool).exists():
            raise SkipTest("build the sv2_test_pool target first")

    def fixture(self):
        self.pool_stderr = open(Path(self.options.tmpdir) / "pool-stderr.log", "a", encoding="utf8")
        self.pool = subprocess.Popen([self.options.sv2_test_pool, str(self.pool_config)],
                                     stdout=subprocess.PIPE, stderr=self.pool_stderr, text=True)
        ready = self.pool.stdout.readline()
        if not ready:
            raise AssertionError("SV2 test pool did not start")
        return json.loads(ready)

    def mode(self, value):
        config = json.loads(self.pool_config.read_text())
        config["mode"] = value
        self.pool_config.write_text(json.dumps(config))

    def events(self):
        if not self.pool_events.exists():
            return []
        return [json.loads(line) for line in self.pool_events.read_text().splitlines(keepends=True) if line.endswith("\n")]

    def miner(self):
        client = StratumClient(self.miner_port)
        client.send(1, "mining.subscribe", ["purity-functional-miner"])
        subscribed, _ = client.receive_id(1)
        client.send(2, "mining.authorize", ["miner.worker", "test-only-password"])
        authorized, seen = client.receive_id(2)
        assert_equal(authorized["result"], True)
        while not any(m.get("method") == "mining.notify" for m in seen):
            seen.append(client.receive())
        notify = next(m for m in reversed(seen) if m.get("method") == "mining.notify")
        return client, subscribed["result"], notify["params"]

    def parent(self, job):
        prev = bytes.fromhex(job[1])
        return b"".join(prev[i:i + 4][::-1] for i in range(0, 32, 4))[::-1].hex()

    def solve(self, subscribed, job, block, start=0):
        extra = "00" * subscribed[2]
        coinbase = bytes.fromhex(job[2] + subscribed[1] + extra + job[3])
        if self.options.coinbase_tags:
            assert b"/TestPool/" in coinbase
            assert b"/TestMiner/" in coinbase
            assert b"/ConfigPool/" not in coinbase
            assert b"/ConfigMiner/" not in coinbase
        else:
            assert b"/TestPool/" not in coinbase
        root = hash256(coinbase)
        for branch in job[4]:
            root = hash256(root + bytes.fromhex(branch))
        prev = bytes.fromhex(job[1])
        prev = b"".join(prev[i:i + 4][::-1] for i in range(0, 32, 4))
        prefix = int(job[5], 16).to_bytes(4, "little") + prev + root
        prefix += int(job[7], 16).to_bytes(4, "little") + int(job[6], 16).to_bytes(4, "little")
        target = uint256_from_compact(int(job[6], 16))
        for nonce in range(start, start + 100000):
            achieved = int.from_bytes(hash256(prefix + nonce.to_bytes(4, "little")), "little")
            if (achieved <= target) == block:
                return ["miner.worker", job[0], extra, job[7], f"{nonce:08x}"], nonce
        raise AssertionError("regtest external miner failed to find requested proof of work")

    def run_test(self):
        node, peer = self.nodes
        assert_equal(node.getsv2info()["enabled"], False)
        wallet = MiniWallet(node)
        self.generate(wallet, 101)
        wallet.send_self_transfer(from_node=node)
        self.sync_all()
        self.miner_port = p2p_port(11)
        self.pool_config = Path(self.options.tmpdir) / "sv2-pool.json"
        self.pool_events = Path(self.options.tmpdir) / "sv2-pool-events.jsonl"
        self.pool_config.write_text(json.dumps({"mining": f"127.0.0.1:{p2p_port(9)}",
                                               "jds": f"127.0.0.1:{p2p_port(10)}",
                                               "events": str(self.pool_events), "mode": "", "extranonce_size": self.options.extranonce_size}))
        ready = self.fixture()
        args = ["-sv2=1", f"-sv2port={self.miner_port}", f"-sv2pool={ready['mining']}",
                f"-sv2jds={ready['jds']}", f"-sv2poolkey={ready['public_key']}",
                f"-sv2jdskey={ready['public_key']}", "-sv2identity=test-pool-account",
                "-sv2auth=1", "-sv2user=miner", "-sv2password=test-only-password"]
        with node.bitcoinconf.open("a", encoding="utf8") as config:
            config.write("\nsv2jobinterval=60\n")
        if self.options.job_interval == 30:
            args += ["-sv2jobinterval=30"]
        if self.options.coinbase_tags:
            with node.bitcoinconf.open("a", encoding="utf8") as config:
                config.write("\nsv2pooltag=/ConfigPool/\nsv2minertag=/ConfigMiner/\n")
            args += ["-sv2pooltag=/TestPool/", "-sv2minertag=/TestMiner/"]
        try:
            self.stop_node(0)
            node.assert_start_raises_init_error(extra_args=args + ["-sv2port=0"],
                expected_msg="-sv2port must be between 1 and 65535", match=ErrorMatch.PARTIAL_REGEX)
            for interval in (29, 61):
                node.assert_start_raises_init_error(extra_args=args + [f"-sv2jobinterval={interval}"],
                    expected_msg="-sv2jobinterval must be between 30 and 60 seconds", match=ErrorMatch.PARTIAL_REGEX)
            if self.is_datum_compiled():
                node.assert_start_raises_init_error(extra_args=args + ["-datum=1"],
                    expected_msg="DATUM Solo and SV2 pooled mining cannot be enabled together", match=ErrorMatch.PARTIAL_REGEX)
            self.start_node(0, extra_args=args)
            self.connect_nodes(0, 1)
            self.wait_until(lambda: node.getsv2info()["running"], timeout=40)
            self.wait_until(lambda: any(e["event"] == "declared" and e["transactions"] > 0 for e in self.events()))
            assert 0 < node.getsv2info()["share_difficulty"] < 1
            self.log.info("Mine actual non-block and block proofs through V1")
            client, subscribed, job = self.miner()
            other, other_subscribed, _ = self.miner()
            assert subscribed[1] != other_subscribed[1]
            other.close()
            bad = StratumClient(self.miner_port)
            bad.send(90, "mining.authorize", ["miner.worker", "incorrect-test-password"])
            assert_equal(bad.receive_id(90)[0]["result"], False)
            bad.close()
            client.send(10, "mining.configure", [["version-rolling"], {"version-rolling.mask": "ffffffff", "version-rolling.min-bit-count": 16}])
            configured = client.receive_id(10)[0]["result"]
            assert_equal(configured["version-rolling"], True)
            assert_equal(configured["version-rolling.mask"], "1fffe000")
            share, nonce = self.solve(subscribed, job, False)
            self.log.info("Same-parent template refresh preserves delayed J1 shares and block solutions")
            node.setmocktime(int(time.time()) + 5)
            old_parent = node.getbestblockhash()
            old_info = node.getsv2info()
            old_job = old_info["job_id"]
            wallet.rescan_utxos()
            wallet.send_self_transfer(from_node=node)
            self.wait_until(lambda: node.getsv2info()["job_id"] != old_job,
                            timeout=self.options.job_interval + 20)
            refresh_seconds = node.getsv2info()["job_created"] - old_info["job_created"]
            assert refresh_seconds >= self.options.job_interval - 1
            if self.options.job_interval == 30:
                assert refresh_seconds < 60  # CLI overrides the 60-second configuration value.
            assert_equal(node.getbestblockhash(), old_parent)
            while True:
                updated = client.receive()
                if updated.get("method") == "mining.notify" and updated["params"][0] != job[0]:
                    assert_equal(updated["params"][8], False)
                    break
            client.send(3, "mining.submit", share)
            result = client.receive_id(3)[0]["result"]
            if not result:
                raise AssertionError(f"Valid external share rejected: {node.getsv2info()}")
            self.wait_until(lambda: node.getsv2info()["accepted_shares"] == 1)
            rejected_before = node.getsv2info()["rejected_shares"]
            client.send(4, "mining.submit", share)
            assert_equal(client.receive_id(4)[0]["result"], False)
            assert_equal(node.getsv2info()["rejected_shares"], rejected_before + 1)
            unknown = list(share)
            unknown[1] = "unknown-job"
            client.send(41, "mining.submit", unknown)
            assert_equal(client.receive_id(41)[0]["result"], False)
            assert_equal(node.getsv2info()["rejected_shares"], rejected_before + 2)
            assert_equal(node.getsv2info()["accepted_shares"], 1)
            height = node.getblockcount()
            rolled_job = list(job)
            rolled_version = int(job[5], 16) ^ 0x2000
            rolled_job[5] = f"{rolled_version:08x}"
            solution, _ = self.solve(subscribed, rolled_job, True, nonce + 1)
            solution.append(f"{rolled_version & 0x1fffe000:08x}")
            client.send(5, "mining.submit", solution)
            self.wait_until(lambda: node.getblockcount() == height + 1)
            self.wait_until(lambda: node.getsv2info()["blocks_accepted"] == 1)
            result = node.getsv2info()
            assert_equal(result["last_block_result"], "accepted")
            assert_equal(result["last_block_hash"], node.getbestblockhash())
            assert result["last_block_time"] > 0
            self.sync_blocks()
            assert_equal(peer.getbestblockhash(), node.getbestblockhash())
            self.wait_until(lambda: any(e["event"] == "block" for e in self.events()))
            block = from_hex(CBlock(), next(e["hex"] for e in self.events() if e["event"] == "block"))
            block.calc_sha256()
            assert_equal(block.hash, node.getbestblockhash())
            assert_equal(block.nVersion, rolled_version)
            assert peer.submitblock(block.serialize().hex()) in (None, "duplicate")
            client.close()
            self.wait_until(lambda: node.getsv2info()["accepted_shares"] >= 2)

            self.log.info("A competing regtest branch replaces stale local mining work")
            self.disconnect_nodes(0, 1)
            self.generate(node, 1, sync_fun=self.no_op)
            self.wait_until(lambda: node.getsv2info()["running"] and
                            node.getsv2info()["previous_block_hash"] == node.getbestblockhash())
            old_client, old_subscribed, old_job = self.miner()
            stale_share, _ = self.solve(old_subscribed, old_job, False)
            self.generate(peer, 2, sync_fun=self.no_op)
            replacement = peer.getbestblockhash()
            self.connect_nodes(0, 1)
            self.sync_blocks()
            assert_equal(node.getbestblockhash(), replacement)
            self.wait_until(lambda: node.getsv2info()["running"] and
                            node.getsv2info()["previous_block_hash"] == replacement)
            self.log.info("Connected miners stay subscribed and receive a clean job for the new parent")
            while True:
                update = old_client.receive()
                if update.get("method") == "mining.notify" and self.parent(update["params"]) == replacement:
                    assert_equal(update["params"][8], True)
                    break
            old_client.send(71, "mining.submit", stale_share)
            assert_equal(old_client.receive_id(71)[0]["result"], False)
            old_client.close()
            fresh, _, _ = self.miner()
            accepted_before = node.getsv2info()["accepted_shares"]
            fresh.send(7, "mining.submit", stale_share)
            assert_equal(fresh.receive_id(7)[0]["result"], False)
            assert_equal(node.getsv2info()["accepted_shares"], accepted_before)
            fresh.close()
            expected_height = node.getblockcount()

            self.log.info("Unacknowledged work remains pending; outage closes miners without Solo fallback")
            self.mode("delay-ack")
            client, subscribed, job = self.miner()
            share, _ = self.solve(subscribed, job, False)
            before = node.getsv2info()["accepted_shares"]
            client.send(6, "mining.submit", share)
            self.wait_until(lambda: node.getsv2info()["pending_shares"] == 1)
            assert_equal(node.getsv2info()["accepted_shares"], before)
            abandoned_before = node.getsv2info()["abandoned_shares"]
            self.pool.terminate()
            self.pool.wait(timeout=5)
            self.wait_until(lambda: not node.getsv2info()["running"])
            self.wait_until(lambda: node.getsv2info()["clients"] == 0)
            assert_equal(node.getsv2info()["accepted_shares"], before)
            assert_equal(node.getsv2info()["pending_shares"], 0)
            assert_equal(node.getsv2info()["abandoned_shares"], abandoned_before + 1)
            assert_equal(node.getblockcount(), expected_height)
            client.close()
            self.mode("")
            self.pool.stdout.close()
            self.pool_stderr.close()
            self.fixture()
            self.wait_until(lambda: node.getsv2info()["running"], timeout=40)

            self.log.info("Pool rejection pauses work; recovery re-declares local templates")
            self.mode("reject-job")
            wallet.rescan_utxos()
            wallet.send_self_transfer(from_node=node)
            self.wait_until(lambda: not node.getsv2info()["running"], timeout=self.options.job_interval + 20)
            self.mode("")
            self.wait_until(lambda: node.getsv2info()["running"], timeout=40)
            info = node.getsv2info()
            for secret in ("test-only-password", "test-pool-account", "miner.worker"):
                assert secret not in json.dumps(info, default=str)
            self.log.info("Invalid PoW stays local; incompatible negotiation and JDS timeout never issue work")
            wallet.rescan_utxos()
            wallet.send_self_transfer(from_node=node)
            assert len(node.getrawmempool()) >= 2
            self.mode("strict-target")
            self.restart_node(0, extra_args=args)
            self.wait_until(lambda: node.getsv2info()["running"], timeout=40)
            self.wait_until(lambda: node.getsv2info()["transactions"] >= 2, timeout=self.options.job_interval + 20)
            assert_equal(node.getsv2info()["share_difficulty"], 1)
            invalid_client, invalid_subscribed, invalid_job = self.miner()
            invalid, _ = self.solve(invalid_subscribed, invalid_job, False)
            invalid_client.send(91, "mining.submit", invalid)
            assert_equal(invalid_client.receive_id(91)[0]["result"], False)
            invalid_client.close()
            assert_equal(node.getsv2info()["local_valid_shares"], 0)
            assert_equal(node.getsv2info()["accepted_shares"], 0)
            for fault in ("duplicate-missing", "bad-version", "fixed-version", "missing-features", "silent-job"):
                self.mode(fault)
                self.restart_node(0, extra_args=args)
                self.wait_until(lambda: node.getsv2info()["status"] == "retrying", timeout=40)
                if fault == "duplicate-missing":
                    self.wait_until(lambda: any(e["event"] == "duplicate-missing-request" and e["transactions"] >= 2 for e in self.events()))
                    assert not any(e["event"] == "unexpected-duplicate-response" for e in self.events())
                info = node.getsv2info()
                assert info["stage"]
                assert info["error"]
                if fault == "missing-features":
                    assert "unsupported-feature-flags" in info["error"]
                unavailable = StratumClient(self.miner_port)
                unavailable.send(901, "mining.subscribe", [])
                response, _ = unavailable.receive_id(901)
                assert_equal(response["error"][0], 20)
                assert_equal(response["result"], None)
                assert_equal(unavailable.stream.readline(), b"")
                unavailable.close()
                assert_equal(node.getsv2info()["running"], False)
                assert_equal(node.getsv2info()["authorized_clients"], 0)
                assert_equal(node.getblockcount(), expected_height)
            self.mode("")
            self.log.info("Wrong authority cannot produce work and node shutdown is bounded")
            wrong = [a for a in args if not a.startswith("-sv2poolkey=")]
            wrong += ["-sv2poolkey=79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"]
            self.restart_node(0, extra_args=wrong)
            self.wait_until(lambda: node.getsv2info()["status"] == "retrying", timeout=20)
            assert_equal(node.getsv2info()["running"], False)
            assert_equal(node.getblockcount(), expected_height)
            start = time.monotonic()
            self.stop_node(0)
            assert time.monotonic() - start < 5
            self.start_node(0)
            assert_equal(node.getsv2info()["enabled"], False)
        finally:
            if self.pool.poll() is None:
                self.pool.terminate()
                self.pool.wait(timeout=5)
            self.pool.stdout.close()
            self.pool_stderr.close()


if __name__ == "__main__":
    Sv2Test(__file__).main()
