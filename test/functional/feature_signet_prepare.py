#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Late activation freeze, cached legacy branches, and same-datadir migration."""

import json
from pathlib import Path
import subprocess

from feature_purity_signet import PuritySignetTest
from feature_signet import signet_blocks
from test_framework.messages import CBlock, CBlockHeader, from_hex
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.test_node import ErrorMatch


class SignetPrepareTest(PuritySignetTest):
    def set_test_params(self):
        super().set_test_params()
        self.num_nodes = 3
        self.fixed_file = self.params_file
        preparing = dict(self.params, schema_version=2)
        for field in ("activation_height", "parent_hash", "anchor", "activation_hash"):
            preparing[field] = None
        self.preparing = preparing

    def setup_chain(self):
        self.params_file = Path(self.options.tmpdir) / "preparing.json"
        self.extra_args = [[f"-signetparams={self.params_file}", "-checkblockindex=1", "-blocksxor=0", "-mocktime=1598919121"]] * self.num_nodes
        self.params_file.write_text(json.dumps(self.preparing))
        super().setup_chain()

    def setup_network(self):
        self.setup_nodes()

    def run_test(self):
        node = self.nodes[0]
        for raw in signet_blocks:
            assert_equal(node.submitblock(raw), None)
        self.connect_nodes(1, 0)
        self.wait_until(lambda: self.nodes[1].getblockcount() == 10)
        assert_equal(self.nodes[1].gettxoutsetinfo()["hash_serialized_3"], node.gettxoutsetinfo()["hash_serialized_3"])
        info = node.getblockchaininfo()
        assert_equal(info["signet_fork"]["state"], "preparing")
        for field in ("activation_height", "parent_hash", "activation_hash", "anchor_height", "anchor_hash", "anchor_bits", "anchor_parent_hash", "anchor_parent_time"):
            assert_equal(info["signet_fork"][field], None)
        assert_equal(node.getmininginfo()["signet_state"], "preparing")
        for method, args in ((node.getblocktemplate, ({"rules": ["segwit", "signet"]},)),
                             (node.getblocktemplate, ({"mode": "proposal", "data": signet_blocks[-1]},)),
                             (node.rpc.generatetodescriptor, (1, "raw(51)")),
                             (node.rpc.generateblock, ("raw(51)", []))):
            assert_raises_rpc_error(-1, "Signet is preparing", method, *args)

        root = Path(self.config["environment"]["SRCDIR"])
        output = Path(self.options.tmpdir) / "frozen.json"
        command = ["python3", str(root / "contrib/purity-signet/prepare.py"),
                   "--config", str(self.params_file), "--output", str(output), "--ready",
                   "--node-cli", json.dumps([self.options.bitcoincli, f"-datadir={node.datadir_path}", "-signet", f"-signetparams={self.params_file}"])]
        stale = subprocess.run(command, capture_output=True, text=True)
        assert stale.returncode != 0, stale.stdout
        assert "older" in stale.stderr, stale.stderr
        assert not output.exists()
        assert_equal(node.getnetworkinfo()["networkactive"], False)
        result = subprocess.run(command + ["--max-parent-age", "1000000000"], capture_output=True, text=True)
        assert_equal(result.returncode, 0)
        node.wait_until_stopped()
        frozen = json.loads(output.read_text())
        evidence = json.loads(output.with_suffix(output.suffix + ".snapshot.json").read_text())
        assert_equal(evidence["schema_version"], 2)
        assert_equal(evidence["verifychain"]["full_history"]["level"], 2)
        assert_equal(evidence["verifychain"]["full_history"]["depth"], 0)
        assert_equal(evidence["verifychain"]["recent_chainstate"]["level"], 4)
        assert_equal(evidence["verifychain"]["recent_chainstate"]["depth"], 288)
        assert_equal(evidence["verifychain"]["recent_chainstate"]["checked_blocks"], 10)
        assert_equal(frozen["activation_height"], 11)
        assert_equal(frozen["parent_hash"], info["bestblockhash"])
        assert_equal(frozen["minimum_chain_work"], info["chainwork"])
        assert_equal(frozen["anchor"], self.params["anchor"])
        assert_equal(frozen["activation_hash"], None)
        assert_equal(frozen["simulation"], True)

        # Cache the original H block as a fully validated, inactive branch and
        # leave legacy headers and a descendant in the preparation database.
        self.start_node(0)
        old = json.loads((root / "src/test/data/bitcoin_signet_block11.json").read_text())
        assert_equal(node.submitblock(old["raw"]), None)
        assert_equal(node.getblockcount(), 11)
        node.invalidateblock(old["hash"])
        node.parkblock(old["hash"])
        node.reconsiderblock(old["hash"])
        assert_equal(node.getblockcount(), 10)
        legacy = from_hex(CBlock(), old["raw"])
        # A different PoW-valid header at H survives on disk without a block.
        header = CBlockHeader(legacy)
        header.nTime += 3600
        mined = subprocess.run([self.options.bitcoinutil, "grind", header.serialize().hex()], check=True, capture_output=True, text=True)
        node.submitheader(mined.stdout.strip())
        header = from_hex(CBlockHeader(), mined.stdout.strip())
        header.rehash()
        child = CBlockHeader(header)
        child.hashPrevBlock = header.sha256
        child.nTime += 600
        mined_child = subprocess.run([self.options.bitcoinutil, "grind", child.serialize().hex()], check=True, capture_output=True, text=True)
        child = from_hex(CBlockHeader(), mined_child.stdout.strip())
        child.rehash()
        node.submitheader(mined_child.stdout.strip())

        self.stop_node(0)
        self.start_node(0, extra_args=[f"-signetparams={output}", "-checkblockindex=1", "-blocksxor=0"])
        assert_equal(node.getblockcount(), 10)
        template = node.getblocktemplate({"rules": ["segwit", "signet"]})
        assert_equal(template["height"], 11)
        assert_equal(template["previousblockhash"], info["bestblockhash"])
        assert_equal(template["signet_challenge"], self.challenge.hex())
        assert_raises_rpc_error(-8, "Block height out of range", node.getblockhash, 11)
        self.log.info("Conversion survives abrupt exit before normal shutdown")
        node.kill_process()
        self.start_node(0, extra_args=[f"-signetparams={output}", "-checkblockindex=1", "-blocksxor=0"])
        assert_equal(node.getblockcount(), 10)
        node.reconsiderblock(old["hash"])
        node.unparkblock(old["hash"])
        node.reconsiderblock(child.hash)
        assert node.getblockchaininfo()["headers"] <= 11

        assert_equal(node.getblockcount(), 10)
        assert node.submitblock(old["raw"]) in ("bad-signet-blksig", "duplicate-invalid")
        block = self.block()
        assert_equal(node.submitblock(block.serialize().hex()), None)
        frozen["activation_hash"] = block.hash
        frozen["simulation"] = False
        pinned = Path(self.options.tmpdir) / "pinned.json"
        pinned.write_text(json.dumps(frozen))
        self.log.info("A wrong first-block pin cannot poison the accepted active chain")
        wrong = dict(frozen, activation_hash=old["hash"])
        wrong_file = Path(self.options.tmpdir) / "wrong-pin.json"
        wrong_file.write_text(json.dumps(wrong))
        self.stop_node(0)
        node.assert_start_raises_init_error(extra_args=[f"-signetparams={wrong_file}"], expected_msg="Active chain conflicts with configured Signet consensus", match=ErrorMatch.PARTIAL_REGEX)
        self.start_node(0, extra_args=[f"-signetparams={pinned}", "-checkblockindex=1"])
        for args in ([], ["-reindex-chainstate"], ["-reindex"]):
            self.restart_node(0, extra_args=[f"-signetparams={pinned}", "-checkblockindex=1", *args])
            self.wait_until(lambda: node.getblockcount() == 11)
            assert_equal(node.getbestblockhash(), block.hash)
        self.log.info("Pinned parameters synchronize prepared and fresh nodes")
        for index in (1, 2):
            self.restart_node(index, extra_args=[f"-signetparams={pinned}", "-checkblockindex=1"])
            self.connect_nodes(index, 0)
            self.wait_until(lambda: self.nodes[index].getblockcount() == 11)
            assert_equal(self.nodes[index].getbestblockhash(), block.hash)
            assert_equal(self.nodes[index].gettxoutsetinfo()["hash_serialized_3"], node.gettxoutsetinfo()["hash_serialized_3"])
        self.stop_node(0)
        # A published first-block pin cannot be removed on a later restart.
        node.assert_start_raises_init_error(extra_args=[f"-signetparams={output}"], expected_msg="Signet consensus parameters changed", match=ErrorMatch.PARTIAL_REGEX)
        node.assert_start_raises_init_error(extra_args=[f"-signetparams={self.params_file}"], expected_msg="Signet consensus parameters changed", match=ErrorMatch.PARTIAL_REGEX)
        self.start_node(0, extra_args=[f"-signetparams={pinned}"])


if __name__ == "__main__":
    SignetPrepareTest(__file__).main()
