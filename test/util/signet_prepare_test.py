#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Snapshot consistency and fail-closed preparation policy."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("prepare", ROOT / "contrib/purity-signet/prepare.py")
PREPARE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PREPARE)


class PrepareTest(unittest.TestCase):
    def setUp(self):
        self.config = json.loads((ROOT / "contrib/purity-signet/signet-params.json").read_text())
        self.info = dict(chain="signet", initialblockdownload=False, headers=100, blocks=100, pruned=False,
                         bestblockhash="aa" * 32, chainwork="00" * 31 + "ff", signet_fork={"state": "preparing",
                         "postfork_challenge": "52" + "".join("21" + key for key in self.config["public_keys"]) + "53ae"})
        self.headers = {"aa" * 32: dict(height=100, hash="aa" * 32, time=10000, chainwork=self.info["chainwork"]),
                        "bb" * 32: dict(height=96, hash="bb" * 32, bits="1e0377ae", previousblockhash="cc" * 32),
                        "cc" * 32: dict(height=95, hash="cc" * 32, time=9500)}
        self.active = True
        self.verified = True
        self.change = False
        self.calls = []

    def rpc(self, method, *args):
        self.calls.append(method)
        if method == "getblockchaininfo":
            result = copy.deepcopy(self.info)
            if self.change and self.calls.count(method) > 2:
                result["bestblockhash"] = "dd" * 32
            return result
        if method == "getnetworkinfo":
            return {"networkactive": self.active}
        if method == "setnetworkactive":
            self.active = args[0]
        elif method == "verifychain":
            self.assertEqual(args, (4, 0))
            return self.verified
        elif method == "getblockhash":
            self.assertEqual(args, (96,))
            return "bb" * 32
        elif method == "getblockheader":
            return self.headers[args[0]]

    def freeze(self, age=3600):
        return PREPARE.freeze(self.config, self.rpc, age, 10010)

    def test_consistent_snapshot(self):
        fixed, record = self.freeze()
        self.assertEqual(fixed["activation_height"], 101)
        self.assertEqual(fixed["parent_hash"], self.info["bestblockhash"])
        self.assertEqual(fixed["minimum_chain_work"], self.info["chainwork"])
        self.assertEqual(fixed["anchor"]["height"], 96)
        self.assertEqual(fixed["anchor"]["parent_time"], 9500)
        self.assertIsNone(fixed["activation_hash"])
        self.assertTrue(fixed["simulation"])
        self.assertTrue(record["verifychain"]["result"])
        self.assertFalse(self.active)
        self.assertNotIn("stop", self.calls)

    def test_incomplete_sync(self):
        self.info["headers"] += 1
        with self.assertRaisesRegex(ValueError, "finish validating"):
            self.freeze()
        self.assertTrue(self.active)

    def test_ibd(self):
        self.info["initialblockdownload"] = True
        with self.assertRaises(ValueError):
            self.freeze()

    def test_partial_configuration(self):
        self.config["anchor"] = {}
        with self.assertRaisesRegex(ValueError, "preparation configuration"):
            self.freeze()
        self.assertEqual(self.calls, [])

    def test_stale_parent(self):
        with self.assertRaisesRegex(ValueError, "older"):
            self.freeze(age=9)
        self.assertFalse(self.active)

    def test_failed_verification(self):
        self.verified = False
        with self.assertRaisesRegex(ValueError, "verification failed"):
            self.freeze()

    def test_changed_tip(self):
        self.change = True
        with self.assertRaisesRegex(ValueError, "changed"):
            self.freeze()

    def test_inconsistent_anchor(self):
        self.headers["cc" * 32]["height"] = 94
        with self.assertRaisesRegex(ValueError, "inconsistent"):
            self.freeze()

    def test_wrong_keys(self):
        self.info["signet_fork"]["postfork_challenge"] = "51"
        with self.assertRaisesRegex(ValueError, "signing keys"):
            self.freeze()
        self.assertTrue(self.active)


if __name__ == "__main__":
    unittest.main()
