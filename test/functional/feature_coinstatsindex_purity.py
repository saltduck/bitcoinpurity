#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Index permitted unspendable outputs after permanent Purity RDTS activation."""

from test_framework.blocktools import add_witness_commitment, create_block, create_coinbase
from test_framework.messages import COIN, CTxOut
from test_framework.script import CScript, OP_RETURN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class CoinStatsIndexPurityTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-coinstatsindex=1', '-testactivationheight=purity@100']]

    def run_test(self):
        node = self.nodes[0]
        self.generate(node, 100)
        tip = node.getbestblockhash()
        cb = create_coinbase(101, nValue=45)
        # 83-byte OP_RETURN exception and a minimal permanently unspendable script.
        cb.vout += [CTxOut(COIN, CScript([OP_RETURN, b'x' * 80])),
                    CTxOut(COIN, CScript([OP_RETURN]))]
        assert_equal(len(cb.vout[1].scriptPubKey), 83)
        cb.rehash()
        block = create_block(int(tip, 16), cb, node.getblockheader(tip)['time'] + 1)
        block.nBits = int(node.getblocktemplate({'rules': ['segwit']})['bits'], 16)
        add_witness_commitment(block)
        block.solve()
        assert_equal(node.submitblock(block.serialize().hex()), None)
        self.wait_until(lambda: node.getindexinfo()['coinstatsindex']['best_block_height'] == 101)
        indexed = node.gettxoutsetinfo('muhash')
        scanned = node.gettxoutsetinfo('muhash', None, False)
        for field in ['height', 'bestblock', 'muhash', 'txouts', 'bogosize', 'total_amount']:
            assert_equal(indexed[field], scanned[field])
        assert_equal(indexed['total_unspendable_amount'], 55)
        assert_equal(indexed['block_info'], {
            'prevout_spent': 0, 'coinbase': 45, 'new_outputs_ex_coinbase': 0,
            'unspendable': 5,
            'unspendables': {'genesis_block': 0, 'bip30': 0, 'scripts': 2, 'unclaimed_rewards': 3},
        })
        node.invalidateblock(indexed['bestblock'])
        node.reconsiderblock(indexed['bestblock'])
        assert_equal(node.gettxoutsetinfo('muhash'), indexed)
        self.restart_node(0, extra_args=self.extra_args[0])
        assert_equal(node.gettxoutsetinfo('muhash'), indexed)


if __name__ == '__main__':
    CoinStatsIndexPurityTest(__file__).main()
