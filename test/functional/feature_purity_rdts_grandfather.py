#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Permanent RDTS grandfathering survives the underlying BIP9 transitions.

Exercise block validation: mempool policy independently rejects nonstandard
Tapscript, so testmempoolaccept cannot establish grandfathering consensus.
"""

from test_framework.blocktools import add_witness_commitment, create_block, create_coinbase
from test_framework.messages import COutPoint, CTransaction, CTxIn, CTxInWitness, CTxOut
from test_framework.script import CScript, OP_1, OP_ENDIF, OP_IF, taproot_construct
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class PurityRDTSGrandfatherTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            "-testactivationheight=purity@200",
            "-testactivationheight=rdtsgrandfatherfix@200",
            "-vbparams=reduced_data:0:9223372036854775807:0:576",
            "-assumevalid=0",
        ]]

    def make_block(self, txs=()):
        node = self.nodes[0]
        tip = node.getbestblockhash()
        block = create_block(int(tip, 16), create_coinbase(node.getblockcount() + 1),
                             node.getblockheader(tip)["time"] + 1)
        block.nVersion = 0x20000000  # No BIP9 signaling; force deadline transitions.
        block.vtx.extend(txs)
        add_witness_commitment(block)
        block.solve()
        return block

    def mine_to(self, height):
        while self.nodes[0].getblockcount() < height:
            assert_equal(self.nodes[0].submitblock(self.make_block().serialize().hex()), None)

    def run_test(self):
        node = self.nodes[0]
        self.mine_to(198)
        internal_key = bytes.fromhex("79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798")
        script = CScript([OP_1, OP_IF, OP_1, OP_ENDIF])
        taproot = taproot_construct(internal_key, [("if", script)])
        leaf = taproot.leaves["if"]
        control = bytes([leaf.version + taproot.negflag]) + internal_key + leaf.merklebranch
        spends = []
        pre_spends = []
        for coinbase_height, creation_height in zip((1, 2, 3), (199, 200, 201)):
            coinbase = node.getblock(node.getblockhash(coinbase_height), 2)["tx"][0]
            funding = CTransaction()
            funding.vin = [CTxIn(COutPoint(int(coinbase["txid"], 16), 0))]
            output_count = 3 if creation_height == 199 else 1
            value = (50 * 100_000_000 - 1000) // output_count
            funding.vout = [CTxOut(value, taproot.scriptPubKey) for _ in range(output_count)]
            block = self.make_block([funding])
            assert_equal(node.submitblock(block.serialize().hex()), None)
            assert_equal(node.getblockcount(), creation_height)
            for output_index in range(output_count):
                spend = CTransaction()
                spend.vin = [CTxIn(COutPoint(int(funding.rehash(), 16), output_index))]
                spend.vout = [CTxOut(value - 1000, CScript([OP_1]))]
                spend.wit.vtxinwit = [CTxInWitness()]
                spend.wit.vtxinwit[0].scriptWitness.stack = [script, control]
                (pre_spends if creation_height == 199 else spends).append(spend)

        for stage, (height, status) in enumerate(((202, "started"), (432, "locked_in"), (576, "active"))):
            self.mine_to(height)
            assert_equal(node.getdeploymentinfo()["deployments"]["reduced_data"]["bip9"]["status"], status)
            self.log.info(f"At {height} BIP9={status}, equality/post-Purity spends must remain rejected")
            for spend in spends:  # Equality at 200 and strictly after at 201.
                tip = node.getbestblockhash()
                result = node.submitblock(self.make_block([spend]).serialize().hex())
                assert result and "mandatory-script-verify-flag-failed (OP_IF/NOTIF argument must be minimal in tapscript)" in result, result
                assert_equal(node.getbestblockhash(), tip)
            # A pre-Purity output retains its exemption at each transition.
            assert_equal(node.submitblock(self.make_block([pre_spends[stage]]).serialize().hex()), None)


if __name__ == "__main__":
    PurityRDTSGrandfatherTest(__file__).main()
