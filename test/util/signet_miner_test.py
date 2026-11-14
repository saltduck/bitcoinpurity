#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Miner status visibility, output privacy and unchanged acceptance checks."""

from contextlib import ExitStack, redirect_stdout
import copy
import hashlib
import importlib.util
import io
import json
import logging
from pathlib import Path
import runpy
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock, patch

ROOT = Path(__file__).resolve().parents[2]
MINER_PATH = ROOT / "contrib/purity-signet/miner.py"
SPEC = importlib.util.spec_from_file_location("signet_miner", MINER_PATH)
MINER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MINER)
REAL_POPEN = subprocess.Popen
REAL_RPC = MINER.rpc

from test_framework.blocktools import add_witness_commitment, create_block, create_coinbase  # noqa: E402


class MinerLoggingTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="signet-miner-test-")
        self.addCleanup(self.directory.cleanup)
        self.directory = Path(self.directory.name)
        self.params = json.loads((ROOT / "contrib/purity-signet/simulation.json").read_text())
        params_path = self.directory / "params.json"
        params_path.write_text(json.dumps(self.params))
        self.secret = "PRIVATE_TEST_PAYLOAD_DO_NOT_LOG"
        self.config = {
            "schema_version": 1,
            "chain_params": str(params_path),
            "node_cli": ["test-cli", "-rpcpassword=" + self.secret],
            "signer_clis": [["test-cli", "signer-a"], ["test-cli", "signer-b"]],
            "bitcoin_util": "test-solver",
            "reward_script": "51",
            "poll_seconds": 1,
        }
        self.config_path = self.directory / "miner.json"
        self.config_path.write_text(json.dumps(self.config))
        anchor = self.params["anchor"]
        self.challenge = "52" + "".join("21" + key for key in self.params["public_keys"]) + "53ae"
        self.fork = {
            "simulation": self.params["simulation"],
            "minimum_chain_work": self.params["minimum_chain_work"],
            "pow_limit": "00000377ae000000000000000000000000000000000000000000000000000000",
            "activation_height": self.params["activation_height"],
            "parent_hash": self.params["parent_hash"],
            "activation_hash": "0" * 64,
            "anchor_height": anchor["height"],
            "anchor_hash": anchor["hash"],
            "anchor_bits": anchor["bits"],
            "anchor_parent_hash": anchor["parent_hash"],
            "anchor_parent_time": anchor["parent_time"],
            "target_spacing": 600,
            "half_life": 86400,
            "message_start": "0a03cf40",
            "postfork_challenge": self.challenge,
        }
        self.parent_time = 1000
        self.template = {
            "height": 11, "bits": "207fffff", "curtime": 1600,
            "previousblockhash": self.params["parent_hash"], "signet_challenge": self.challenge,
        }
        self.block = create_block(int(self.params["parent_hash"], 16), create_coinbase(11), 1600)
        add_witness_commitment(self.block)
        self.block.vtx[0].vout[-1].scriptPubKey = bytes(self.block.vtx[0].vout[-1].scriptPubKey)
        self.calls = []
        self.signing_requests = []
        self.failed_signers = set()
        self.non_signing = set()
        self.signatures = {self.secret: set()}
        self.tips = []
        self.quorum = True
        self.proposal_rejection = None
        self.submission_rejection = None
        self.active = True
        self.timeouts = 0
        self.interrupt = False
        self.invalid_work = False
        self.changed_header = False
        self.process = MagicMock()
        self.process.__enter__.return_value = self.process
        self.process.__exit__.return_value = False
        self.process.returncode = 0
        self.process.poll.return_value = 0
        self.process.communicate.side_effect = self.communicate
        original = SimpleNamespace(
            g=SimpleNamespace(map={
                MINER.PSBT_GLOBAL_UNSIGNED_TX: b"unsigned-tx",
                MINER.PRIMITIVES["PSBT_SIGNET_BLOCK"]: b"unsigned-block",
            }),
            i=[SimpleNamespace(map={MINER.PSBT_IN_NON_WITNESS_UTXO: b"challenge-tx"})],
        )
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.stack.enter_context(patch.object(MINER, "rpc", side_effect=self.rpc))
        self.parse = self.stack.enter_context(patch.object(MINER.PSBT, "from_base64", return_value=original))
        self.stack.enter_context(patch.dict(MINER.PRIMITIVES, {
            "generate_psbt": lambda *_: self.secret,
            "decode_psbt": lambda *_: (self.block, b"private-test-solution"),
        }))
        self.popen = self.stack.enter_context(patch.object(MINER.subprocess, "Popen", side_effect=self.spawn))
        self.miner = MINER.Miner(self.config_path)

    def rpc(self, argv, method, *args, **kwargs):
        self.calls.append(method)
        if method == "getblockchaininfo":
            return {"chain": "signet", "signet_fork": copy.deepcopy(self.fork)}
        if method == "getblocktemplate":
            return self.proposal_rejection if args[0].get("mode") == "proposal" else self.template.copy()
        if method == "getblockheader":
            return {"time": self.parent_time}
        if method == "walletprocesspsbt":
            signer = argv[-1]
            self.signing_requests.append((signer, args[0], kwargs))
            if signer in self.failed_signers:
                raise RuntimeError("RPC walletprocesspsbt failed (exit 1)")
            signatures = self.signatures[args[0]].copy()
            if signer not in self.non_signing:
                signatures.add(signer)
            signed = self.secret + ":" + ",".join(sorted(signatures))
            self.signatures[signed] = signatures
            return {"psbt": signed}
        if method == "finalizepsbt":
            return {"psbt": args[0], "complete": self.quorum and len(self.signatures[args[0]]) >= 2}
        if method == "getbestblockhash":
            return self.tips.pop(0) if self.tips else self.template["previousblockhash"]
        if method == "submitblock":
            return self.submission_rejection
        if method == "getblockhash":
            return self.block.hash if self.active else "ff" * 32
        raise AssertionError(method)

    def spawn(self, argv, **kwargs):
        raw = bytes.fromhex(argv[2])
        if self.changed_header:
            raw = struct.pack("<I", 99) + raw[4:]
        target = 0x7fffff << (8 * (0x20 - 3))
        for nonce in range(10000):
            candidate = raw[:76] + struct.pack("<I", nonce)
            value = int.from_bytes(hashlib.sha256(hashlib.sha256(candidate).digest()).digest(), "little")
            if (value <= target) != self.invalid_work:
                self.solved = candidate.hex()
                return self.process
        raise AssertionError("Could not create easy test work")

    def communicate(self, *args, **kwargs):
        if self.interrupt:
            self.interrupt = False
            raise KeyboardInterrupt
        if self.timeouts:
            self.timeouts -= 1
            raise subprocess.TimeoutExpired("grind", 1, output=self.secret)
        return self.solved, self.secret

    def test_wait_countdown_without_signing(self):
        self.template["curtime"] = 1590
        with self.assertLogs("purity_signet_miner", level="INFO") as logs:
            self.assertIsNone(self.miner.attempt())
            self.template["curtime"] = 1595
            self.assertIsNone(self.miner.attempt())
        self.assertIn("remaining=10s", "\n".join(logs.output))
        self.assertIn("remaining=5s", "\n".join(logs.output))
        self.assertNotIn("walletprocesspsbt", self.calls)
        self.popen.assert_not_called()

    def test_successful_stages_and_payload_privacy(self):
        with self.assertLogs("purity_signet_miner", level="INFO") as logs:
            result = self.miner.attempt()
        self.assertEqual(result["height"], 11)
        text = "\n".join(logs.output)
        for stage in ("Preparing block", "Signing request", "signer=1/2", "signer=2/2",
                      "Finalizing signatures", "Signature quorum complete", "Checking signed proposal",
                      "PoW started", "PoW complete", "Submitting block", "Block accepted"):
            self.assertIn(stage, text)
        for value in (self.secret, "private-test-solution", "-rpcpassword", self.solved):
            self.assertNotIn(value, text)

    def test_pow_heartbeat_keeps_one_solver(self):
        self.timeouts = 2
        with patch.object(MINER.time, "monotonic", side_effect=[100, 105, 110, 111]):
            with self.assertLogs("purity_signet_miner", level="INFO") as logs:
                self.miner.attempt()
        self.assertEqual(self.popen.call_count, 1)
        text = "\n".join(logs.output)
        self.assertIn("PoW running: height=11 elapsed=5s", text)
        self.assertIn("PoW running: height=11 elapsed=10s", text)
        self.assertEqual(self.process.communicate.call_count, 3)
        self.process.kill.assert_not_called()

    def test_interrupt_reaps_solver(self):
        self.interrupt = True
        with self.assertRaises(KeyboardInterrupt):
            self.miner.attempt()
        self.process.kill.assert_called_once()
        self.assertEqual(self.process.communicate.call_count, 2)

    def test_real_subprocess_survives_heartbeat(self):
        solver = (
            "import hashlib, struct, sys, time\n"
            "time.sleep(1.2)\n"
            "header = bytes.fromhex(sys.argv[1])\n"
            "target = 0x7fffff << (8 * (0x20 - 3))\n"
            "for nonce in range(10000):\n"
            "    candidate = header[:76] + struct.pack('<I', nonce)\n"
            "    value = int.from_bytes(hashlib.sha256(hashlib.sha256(candidate).digest()).digest(), 'little')\n"
            "    if value <= target:\n"
            "        print(candidate.hex())\n"
            "        break\n"
            "sys.stderr.write('PRIVATE_TEST_PAYLOAD_DO_NOT_LOG')\n"
        )
        self.popen.side_effect = lambda argv, **kwargs: REAL_POPEN([sys.executable, "-c", solver, argv[2]], **kwargs)
        with self.assertLogs("purity_signet_miner", level="INFO") as logs:
            result = self.miner.attempt()
        self.assertEqual(result["height"], 11)
        self.assertEqual(self.popen.call_count, 1)
        self.assertIn("PoW running", "\n".join(logs.output))
        self.assertIn("PoW complete", "\n".join(logs.output))
        self.assertNotIn(self.secret, "\n".join(logs.output))

    def test_failed_quorum_never_starts_pow(self):
        self.quorum = False
        with self.assertRaisesRegex(RuntimeError, "signature quorum"):
            self.miner.attempt()
        self.popen.assert_not_called()
        self.assertNotIn("submitblock", self.calls)

    def configure_three_signers(self):
        self.config["signer_clis"].append(["test-cli", "signer-c"])
        self.config_path.write_text(json.dumps(self.config))
        self.miner = MINER.Miner(self.config_path)

    def test_any_single_signer_outage_uses_available_pair(self):
        self.configure_three_signers()
        original_block = copy.deepcopy(self.block)
        for failed, expected, pair in (
            ("signer-a", ["signer-a", "signer-b", "signer-c"], "2+3"),
            ("signer-b", ["signer-a", "signer-b", "signer-a", "signer-c"], "1+3"),
            ("signer-c", ["signer-a", "signer-b"], "1+2"),
        ):
            with self.subTest(failed=failed):
                self.block = copy.deepcopy(original_block)
                self.failed_signers = {failed}
                self.signing_requests.clear()
                with self.assertLogs("purity_signet_miner", level="INFO") as logs:
                    self.assertEqual(self.miner.attempt()["height"], 11)
                self.assertEqual([request[0] for request in self.signing_requests], expected)
                self.assertIn("signers=" + pair, "\n".join(logs.output))
                self.assertNotIn(self.secret, "\n".join(logs.output))
                for _, _, kwargs in self.signing_requests:
                    self.assertEqual(kwargs["timeout"], 30)

    def test_non_signing_wallet_cannot_count_as_quorum(self):
        self.configure_three_signers()
        self.non_signing = {"signer-a"}
        with self.assertLogs("purity_signet_miner", level="INFO") as logs:
            self.assertEqual(self.miner.attempt()["height"], 11)
        self.assertEqual([request[0] for request in self.signing_requests],
                         ["signer-a", "signer-b", "signer-a", "signer-c", "signer-b", "signer-c"])
        self.assertEqual([request[1] for request in self.signing_requests[::2]], [self.secret] * 3)
        self.assertEqual("\n".join(logs.output).count("Signature quorum complete"), 1)
        self.assertIn("signers=2+3", "\n".join(logs.output))

    def test_two_unavailable_or_insufficient_signers_never_solve(self):
        self.configure_three_signers()
        for failed in ({"signer-a", "signer-b"}, set()):
            with self.subTest(failed=failed):
                self.failed_signers = failed
                self.quorum = bool(failed)
                self.calls.clear()
                with self.assertLogs("purity_signet_miner", level="WARNING"):
                    with self.assertRaisesRegex(RuntimeError, "signature quorum"):
                        self.miner.attempt()
                self.popen.assert_not_called()
                self.assertNotIn("submitblock", self.calls)

    def test_mutating_signer_is_excluded_without_reusing_its_psbt(self):
        self.configure_three_signers()
        original = self.parse.return_value
        original_block = copy.deepcopy(self.block)
        for field in (MINER.PSBT_GLOBAL_UNSIGNED_TX, MINER.PRIMITIVES["PSBT_SIGNET_BLOCK"],
                      MINER.PSBT_IN_NON_WITNESS_UTXO, "malformed"):
            with self.subTest(field=field):
                self.block = copy.deepcopy(original_block)
                self.signing_requests.clear()
                changed = copy.deepcopy(original)
                if field == MINER.PSBT_IN_NON_WITNESS_UTXO:
                    changed.i[0].map[field] = b"changed"
                else:
                    changed.g.map[field] = b"changed"

                def parse(value):
                    if "signer-a" in value:
                        if field == "malformed":
                            raise ValueError(self.secret)
                        return changed
                    return original

                self.parse.side_effect = parse
                with self.assertLogs("purity_signet_miner", level="INFO") as logs:
                    self.assertEqual(self.miner.attempt()["height"], 11)
                self.assertEqual([request[0] for request in self.signing_requests],
                                 ["signer-a", "signer-b", "signer-c"])
                self.assertEqual(self.signing_requests[1][1], self.secret)
                self.assertIn("invalid PSBT", "\n".join(logs.output))
                self.assertNotIn(self.secret, "\n".join(logs.output))

    def test_invalid_endpoint_count_duplicates_and_timeout_are_rejected(self):
        valid = copy.deepcopy(self.config)
        for count in (0, 1, 4):
            with self.subTest(count=count):
                self.config = dict(valid, signer_clis=[["test-cli", str(i)] for i in range(count)])
                self.config_path.write_text(json.dumps(self.config))
                with self.assertRaises(ValueError):
                    MINER.Miner(self.config_path)
        for signers in ([valid["signer_clis"][0]] * 2,
                        [*valid["signer_clis"], valid["signer_clis"][0]]):
            self.config_path.write_text(json.dumps(dict(valid, signer_clis=signers)))
            with self.assertRaises(ValueError):
                MINER.Miner(self.config_path)
        for timeout in (0, -1, True, 1.5, "30", None):
            with self.subTest(timeout=timeout):
                self.config_path.write_text(json.dumps(dict(valid, signer_timeout_seconds=timeout)))
                with self.assertRaises(ValueError):
                    MINER.Miner(self.config_path)

    def test_real_signer_rpc_timeout_hides_payloads_and_reaps_child(self):
        self.stack.close()
        command = [sys.executable, "-c", "import sys,time; print('PRIVATE_RPC_PAYLOAD', flush=True); time.sleep(10)"]
        with self.assertRaisesRegex(RuntimeError, "RPC walletprocesspsbt timed out") as error:
            REAL_RPC(command, "walletprocesspsbt", self.secret, timeout=1)
        self.assertNotIn(self.secret, str(error.exception))
        self.assertNotIn("PRIVATE_RPC_PAYLOAD", str(error.exception))

    def test_node_finalization_failure_is_not_a_signer_failure(self):
        self.configure_three_signers()
        original_rpc = self.rpc

        def rpc(argv, method, *args, **kwargs):
            if method == "finalizepsbt":
                raise RuntimeError("RPC finalizepsbt failed (exit 1)")
            return original_rpc(argv, method, *args, **kwargs)

        with patch.object(MINER, "rpc", side_effect=rpc):
            with self.assertRaisesRegex(RuntimeError, "RPC finalizepsbt failed"):
                self.miner.attempt()
        self.assertEqual([request[0] for request in self.signing_requests], ["signer-a", "signer-b"])
        self.popen.assert_not_called()

    def test_failed_over_quorum_still_requires_a_valid_signed_proposal(self):
        self.configure_three_signers()
        self.failed_signers = {"signer-a"}
        self.proposal_rejection = "bad-signet-blksig"
        with self.assertLogs("purity_signet_miner", level="INFO") as logs:
            with self.assertRaisesRegex(RuntimeError, "Signed proposal rejected: bad-signet-blksig"):
                self.miner.attempt()
        self.assertIn("Signature quorum complete: height=11 signers=2+3", "\n".join(logs.output))
        self.popen.assert_not_called()
        self.assertNotIn("submitblock", self.calls)

    def test_stale_work_is_reported_without_submission(self):
        for stage, tips in (("before PoW", ["ff" * 32]),
                            ("after PoW", [self.params["parent_hash"], "ff" * 32])):
            with self.subTest(stage=stage):
                self.calls.clear()
                self.tips = tips
                with self.assertLogs("purity_signet_miner", level="INFO") as logs:
                    self.assertIsNone(self.miner.attempt())
                self.assertIn("Discarding stale block " + stage, "\n".join(logs.output))
                self.assertNotIn("submitblock", self.calls)

    def test_invalid_header_or_work_is_still_rejected(self):
        for changed, invalid, message in ((True, False, "changed signed header"),
                                          (False, True, "insufficient work")):
            with self.subTest(message=message):
                self.calls.clear()
                self.changed_header = changed
                self.invalid_work = invalid
                with self.assertRaisesRegex(RuntimeError, message):
                    self.miner.attempt()
                self.assertNotIn("submitblock", self.calls)

    def test_rejected_submission_is_not_logged_as_accepted(self):
        self.submission_rejection = "bad-test-block"
        with self.assertLogs("purity_signet_miner", level="INFO") as logs:
            with self.assertRaisesRegex(RuntimeError, "Block rejected"):
                self.miner.attempt()
        self.assertIn("Submitting block", "\n".join(logs.output))
        self.assertNotIn("Block accepted", "\n".join(logs.output))

    def test_nonactive_submission_is_reported(self):
        self.active = False
        with self.assertLogs("purity_signet_miner", level="INFO") as logs:
            self.assertIsNone(self.miner.attempt())
        self.assertIn("Submitted block is not active", "\n".join(logs.output))
        self.assertNotIn("Block accepted", "\n".join(logs.output))

    def test_once_stdout_remains_one_json_record(self):
        result = {"height": 11, "hash": "ab" * 32, "bits": "207fffff"}
        self.miner.attempt = MagicMock(return_value=result)
        output = io.StringIO()
        with patch.object(MINER, "Miner", return_value=self.miner):
            with patch.object(sys, "argv", [str(MINER_PATH), "--config", str(self.config_path), "--once"]):
                with self.assertLogs("purity_signet_miner", level="INFO"):
                    with redirect_stdout(output):
                        MINER.main()
        self.assertEqual(output.getvalue(), json.dumps(result) + "\n")

    def test_continuous_failure_reports_retry_interval(self):
        self.miner.attempt = MagicMock(side_effect=RuntimeError("RPC walletprocesspsbt failed (exit 1)"))
        output = io.StringIO()
        with patch.object(MINER, "Miner", return_value=self.miner):
            with patch.object(sys, "argv", [str(MINER_PATH), "--config", str(self.config_path)]):
                with patch.object(MINER.time, "sleep", side_effect=KeyboardInterrupt) as sleep:
                    with self.assertLogs("purity_signet_miner", level="INFO") as logs:
                        with redirect_stdout(output):
                            with self.assertRaises(KeyboardInterrupt):
                                MINER.main()
        self.assertIn("retrying in 1s", "\n".join(logs.output))
        sleep.assert_called_once_with(1)
        self.assertEqual(output.getvalue(), "")

    def test_cli_errors_are_timestamped_without_rpc_secrets(self):
        self.stack.close()
        fake_cli = self.directory / "fake-cli.py"
        fake_cli.write_text("import sys\nsys.stderr.write('PRIVATE_RPC_ERROR_PAYLOAD')\nsys.exit(1)\n")
        self.config["node_cli"] = [sys.executable, str(fake_cli), "-rpcpassword=" + self.secret]
        self.config_path.write_text(json.dumps(self.config))
        result = subprocess.run([sys.executable, str(MINER_PATH), "--config", str(self.config_path), "--once"],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertIn("Starting Signet miner", result.stderr)
        self.assertIn("RPC getblockchaininfo failed", result.stderr)
        self.assertRegex(result.stderr, r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{4} INFO")
        self.assertNotIn(self.secret, result.stderr)
        self.assertNotIn("PRIVATE_RPC_ERROR_PAYLOAD", result.stderr)

    def test_import_does_not_configure_root_logger(self):
        handlers = logging.getLogger().handlers.copy()
        with patch.object(logging, "basicConfig") as configure:
            runpy.run_path(str(MINER_PATH), run_name="miner_import_check")
        configure.assert_not_called()
        self.assertEqual(logging.getLogger().handlers, handlers)


if __name__ == "__main__":
    unittest.main()
