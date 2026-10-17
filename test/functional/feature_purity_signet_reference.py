#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Compare the full simulated prefix with an external original Signet node."""

from pathlib import Path
import json

from feature_purity_signet import PuritySignetTest
from feature_signet import signet_blocks
from test_framework.test_framework import SkipTest
from test_framework.util import assert_equal
from test_framework.test_node import ErrorMatch


class PuritySignetReferenceTest(PuritySignetTest):
    def add_options(self, parser):
        parser.add_argument("--reference-bitcoind", help="Original Bitcoin Signet binary; no Purity fork options")

    def set_test_params(self):
        super().set_test_params()
        self.num_nodes = 2
        self.supports_cli = False
        self.extra_args += [["-assumevalid=0", "-minimumchainwork=0", "-maxconnections=20"]]

    def skip_test_if_missing_module(self):
        super().skip_test_if_missing_module()
        if not self.options.reference_bitcoind:
            raise SkipTest("An original Signet reference binary is required")

    def setup_network(self):
        self.add_nodes(2, extra_args=self.extra_args,
                       binary=[self.options.bitcoind, self.options.reference_bitcoind],
                       binary_cli=[self.options.bitcoincli] * 2, versions=[None, 280000])
        self.start_nodes()

    def run_test(self):
        node, reference = self.nodes
        for raw in signet_blocks:
            assert_equal(reference.submitblock(raw), None)
        self.connect_nodes(0, 1)
        self.wait_until(lambda: node.getblockcount() == 10)
        for height in range(11):
            assert_equal(node.getblockhash(height), reference.getblockhash(height))
        for field in ("hash_serialized_3", "total_amount", "txouts"):
            assert_equal(node.gettxoutsetinfo()[field], reference.gettxoutsetinfo()[field])
        assert_equal(node.chain_path.name, "signet")
        assert_equal(reference.chain_path.name, "signet")
        assert node.chain_path != reference.chain_path
        block = self.block()
        assert_equal(node.submitblock(block.serialize().hex()), None)
        self.wait_until(lambda: node.getblockcount() == 11)
        assert_equal(reference.getblockcount(), 10)
        assert_equal(reference.listbanned(), [])
        old_fixture = json.loads((Path(self.config["environment"]["SRCDIR"]) / "src/test/data/bitcoin_signet_block11.json").read_text())
        assert_equal(reference.submitblock(old_fixture["raw"]), None)
        assert_equal(reference.getblockhash(11), old_fixture["hash"])
        assert node.getblockhash(11) != reference.getblockhash(11)
        pinned = self.params.copy()
        pinned["activation_hash"] = block.hash
        artifact_dir = Path(self.options.tmpdir)
        (artifact_dir / "simulation-pinned.json").write_text(json.dumps(pinned, indent=2) + "\n")
        (artifact_dir / "first-fork-block.hex").write_text(block.serialize().hex() + "\n")
        self.restart_node(0, extra_args=[f"-signetparams={artifact_dir / 'simulation-pinned.json'}", "-checkblockindex=1"])
        assert_equal(node.getbestblockhash(), block.hash)

        self.log.info("The same signet directory cannot silently adopt original post-fork state")
        self.stop_node(1)
        reference.binary = self.options.bitcoind
        reference.args[0] = self.options.bitcoind
        reference.assert_start_raises_init_error(
            extra_args=[f"-signetparams={self.params_file}", "-checklevel=0", "-checkblocks=1"],
            expected_msg="Active chain conflicts with configured Signet consensus", match=ErrorMatch.PARTIAL_REGEX)



if __name__ == "__main__":
    PuritySignetReferenceTest(__file__).main()
