#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Real Signet history, height-dependent authority, RDTS and disk replay."""

import copy
import json
from pathlib import Path
import runpy
import subprocess

from feature_signet import signet_blocks
from test_framework.key import ECKey
from test_framework.messages import CBlock, CBlockHeader, CTxOut, from_binary, from_hex, ser_string
from test_framework.psbt import PSBT
from test_framework.script import CScript, CScriptOp, LegacySignatureHash, OP_0, OP_TRUE, OP_RETURN, OP_CHECKMULTISIG, SIGHASH_ALL
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class PuritySignetTest(BitcoinTestFramework):
    def set_test_params(self):
        self.chain = "signet"
        self.num_nodes = 1
        self.setup_clean_chain = True
        root = Path(self.config["environment"]["SRCDIR"])
        self.params_file = root / "contrib/purity-signet/simulation.json"
        self.params = json.loads(self.params_file.read_text())
        self.extra_args = [[f"-signetparams={self.params_file}", "-checkblockindex=1", "-blocksxor=0"]]
        self.miner = runpy.run_path(str(root / "contrib/signet/miner"), run_name="signet_primitives")
        self.challenge = CScript([2, *[bytes.fromhex(k) for k in self.params["public_keys"]], 3, OP_CHECKMULTISIG])

    def skip_test_if_missing_module(self):
        self.skip_if_no_bitcoin_util()

    def block(self, *, signers=(42, 43), extra_output=False, mine=True):
        node = self.nodes[0]
        template = node.getblocktemplate({"rules": ["segwit", "signet"]})
        assert_equal(template["signet_challenge"], self.challenge.hex())
        psbt = PSBT.from_base64(self.miner["generate_psbt"](template, bytes(CScript([OP_TRUE]))))
        block = from_binary(CBlock, psbt.g.map[self.miner["PSBT_SIGNET_BLOCK"]])
        if extra_output:
            block.vtx[0].vout.insert(1, CTxOut(0, CScript([OP_RETURN, b"x" * 81])))
        signme, _ = self.miner["signet_txs"](block, bytes(self.challenge))
        digest, error = LegacySignatureHash(self.challenge, signme, 0, SIGHASH_ALL)
        assert error is None, error
        signatures = []
        for value in signers:
            key = ECKey()
            key.set(value.to_bytes(32, "big"), True)
            signatures.append(key.sign_ecdsa(digest) + bytes([SIGHASH_ALL]))
        solution = ser_string(bytes(CScript([OP_0, *signatures]))) + b"\x00"
        block.vtx[0].vout[-1].scriptPubKey += CScriptOp.encode_op_pushdata(b"\xec\xc7\xda\xa2" + solution)
        block.vtx[0].rehash()
        block.hashMerkleRoot = block.calc_merkle_root()
        block.rehash()
        if mine:
            result = subprocess.run([self.options.bitcoinutil, "grind", CBlockHeader(block).serialize().hex()], check=True, capture_output=True, text=True)
            block.nNonce = from_hex(CBlockHeader(), result.stdout.strip()).nNonce
            block.rehash()
        return block

    def proposal(self, block):
        return self.nodes[0].getblocktemplate({"mode": "proposal", "data": block.serialize().hex()})

    def run_test(self):
        node = self.nodes[0]
        self.log.info("Replay the real first ten original Signet blocks")
        for height, raw in enumerate(signet_blocks, 1):
            assert_equal(node.submitblock(raw), None)
            expected = from_hex(CBlock(), raw)
            expected.rehash()
            assert_equal(node.getblockhash(height), expected.hash)
        assert_equal(node.getbestblockhash(), self.params["parent_hash"])
        template = node.getblocktemplate({"rules": ["segwit", "signet"]})
        assert_equal(template["height"], 11)
        assert_equal(template["signet_challenge"], self.challenge.hex())
        assert_equal(node.getblockchaininfo()["signet_challenge_height"], 11)
        assert_equal(node.getmininginfo()["signet_challenge_height"], 11)

        self.log.info("Unsigned proposals and wrong authority cannot pass")
        old_fixture = json.loads((Path(self.config["environment"]["SRCDIR"]) / "src/test/data/bitcoin_signet_block11.json").read_text())
        old_block = from_hex(CBlock(), old_fixture["raw"])
        old_block.nBits = int(template["bits"], 16)
        old_block.rehash()
        assert_equal(self.proposal(old_block), "bad-signet-blksig")
        bad = self.block(signers=(9, 10), mine=False)
        assert_equal(self.proposal(bad), "bad-signet-blksig")
        unsigned = self.block(mine=False)
        unsigned.vtx[0].vout[-1].scriptPubKey = unsigned.vtx[0].vout[-1].scriptPubKey[:38]
        unsigned.vtx[0].rehash()
        unsigned.hashMerkleRoot = unsigned.calc_merkle_root()
        unsigned.rehash()
        assert_equal(self.proposal(unsigned), "bad-signet-blksig")

        self.log.info("A real signed PoW block can carry the canonical commitment")
        block = self.block()
        assert len(block.vtx[0].vout[-1].scriptPubKey) > 83
        assert_equal(node.submitblock(block.serialize().hex()), None)
        assert_equal(node.getbestblockhash(), block.hash)
        assert_equal(node.getblockcount(), 11)
        assert_equal(node.getblock(block.hash)["height"], 11)

        self.log.info("Other Coinbase outputs retain the ordinary size limit")
        assert_equal(self.proposal(self.block(extra_output=True, mine=False)), "bad-txns-vout-script-toolarge")
        harder = self.block(mine=False)
        harder.nBits -= 1
        harder.rehash()
        assert_equal(self.proposal(harder), "bad-diffbits")
        next_block = self.block(signers=(43, 44))
        assert_equal(node.submitblock(next_block.serialize().hex()), None)
        tip = next_block.hash

        self.log.info("Disconnect and reconnect across H preserve the UTXO state")
        utxo = node.gettxoutsetinfo()["hash_serialized_3"]
        historical = node.getblockhash(9)
        node.invalidateblock(historical)
        assert_equal(node.getblockcount(), 8)
        assert_equal(node.getblockchaininfo()["signet_challenge_height"], 9)
        node.reconsiderblock(historical)
        assert_equal(node.getbestblockhash(), tip)
        assert_equal(node.gettxoutsetinfo()["hash_serialized_3"], utxo)

        self.log.info("Restart, chainstate-only replay and full reindex recheck signatures")
        for extra in ([], ["-reindex-chainstate"], ["-reindex"]):
            self.restart_node(0, extra_args=self.extra_args[0] + extra + ["-checklevel=4", "-checkblocks=0"])
            node = self.nodes[0]
            self.wait_until(lambda: node.getbestblockhash() == tip)
            assert_equal(node.verifychain(4, 0), True)
            assert_equal(node.getblockcount(), 12)
        self.log.info("Signet history, new authority and disk replay passed")

        self.log.info("Frozen anchor metadata cannot be changed with an existing index")
        changed = copy.deepcopy(self.params)
        changed["anchor"]["bits"] = "1e0377ad"
        changed_file = Path(self.options.tmpdir) / "changed-anchor.json"
        changed_file.write_text(json.dumps(changed))
        self.stop_node(0)
        node.rpc_timeout = 5
        node.assert_start_raises_init_error(extra_args=[f"-signetparams={changed_file}"])
        node.rpc_timeout = self.rpc_timeout
        self.start_node(0, extra_args=self.extra_args[0])

        self.log.info("Cached script status cannot hide a corrupted on-disk Signet signature")
        block_file = node.chain_path / "blocks/blk00000.dat"
        original_bytes = block_file.read_bytes()
        raw = next_block.serialize()
        position = original_bytes.index(raw) + raw.index(bytes(next_block.vtx[0].vout[-1].scriptPubKey))
        offset = position + len(next_block.vtx[0].vout[-1].scriptPubKey) - 10
        corrupted = bytearray(original_bytes)
        corrupted[offset] ^= 1
        witness_corrupted = bytearray(original_bytes)
        witness_corrupted[original_bytes.index(raw) + len(raw) - 5] ^= 1
        for broken in (corrupted, witness_corrupted):
            block_file.write_bytes(broken)
            try:
                assert_raises_rpc_error(-1, "Block not found on disk", node.getblock, tip, 0)
            finally:
                block_file.write_bytes(original_bytes)
        self.stop_node(0)
        block_file.write_bytes(corrupted)
        with node.assert_debug_log(["Errors in Signet block data", "ReadBlock failed at 12"]):
            node.assert_start_raises_init_error(extra_args=self.extra_args[0] + ["-checkblocks=1", "-checklevel=0"])
        block_file.write_bytes(original_bytes)
        self.start_node(0, extra_args=self.extra_args[0])

        self.log.info("The signed challenge also binds authority keys not used by the last block")
        changed = copy.deepcopy(self.params)
        key = ECKey()
        key.set((45).to_bytes(32, "big"), True)
        changed["public_keys"][0] = key.get_pubkey().get_bytes().hex()
        changed_file.write_text(json.dumps(changed))
        self.stop_node(0)
        node.rpc_timeout = 5
        node.assert_start_raises_init_error(extra_args=[f"-signetparams={changed_file}", "-checkblocks=1", "-checklevel=0"])
        node.rpc_timeout = self.rpc_timeout
        self.start_node(0, extra_args=self.extra_args[0])


if __name__ == "__main__":
    PuritySignetTest(__file__).main()
