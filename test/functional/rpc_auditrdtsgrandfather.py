#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license.
"""Read-only historical RDTS audit, ranges, missing data and CLI conversion."""

import hashlib

from test_framework.blocktools import create_block, create_coinbase
from test_framework.script import CScript, OP_NOP, OP_RETURN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.wallet import MiniWallet


class AuditRdtsGrandfatherTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-prune=1', '-fastprune']]

    def state(self):
        node = self.nodes[0]
        return (node.getbestblockhash(), node.getblockcount(),
                node.gettxoutsetinfo()['hash_serialized_3'], node.getchaintips(),
                node.getblockheader(node.getbestblockhash()))

    def history_files(self):
        return {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                for pattern in ('blk*.dat', 'rev*.dat')
                for p in self.nodes[0].blocks_path.glob(pattern)}

    def run_test(self):
        node = self.nodes[0]
        wallet = MiniWallet(node)
        self.generate(wallet, 110)
        transfer = wallet.send_self_transfer(from_node=node)
        wallet.send_self_transfer(from_node=node, utxo_to_spend=transfer['new_utxo'])
        self.generate(wallet, 1)
        tip = node.getblockcount()
        before = self.state()
        disk_before = self.history_files()
        with node.assert_debug_log(['RDTS-GRANDFATHER-AUDIT progress', 'RDTS-GRANDFATHER-AUDIT SUMMARY']):
            result = node.auditrdtsgrandfather(1, tip)
        assert_equal(result['start_height'], 1)
        assert_equal(result['end_height'], tip)
        assert_equal(result['snapshot_tip_height'], tip)
        assert_equal(result['snapshot_tip_hash'], before[0])
        assert_equal(result['blocks_scanned'], tip)
        assert_equal(result['transactions_scanned'], tip + 2)
        assert_equal(result['inputs_scanned'], 2)
        for key in ('candidate_inputs', 'old_pass_strict_pass', 'old_pass_strict_fail',
                    'old_fail_strict_fail', 'old_fail_strict_pass', 'missing_block_data',
                    'missing_undo_data', 'invalid_undo_data', 'failures_total', 'failures_returned'):
            assert_equal(result[key], 0)
        assert_equal(result['complete'], True)
        assert_equal(result['chain_changed_during_scan'], False)
        assert_equal(result['interrupted'], False)
        assert_equal(result['failures_truncated'], False)
        assert_equal(result['failures'], [])
        assert_equal(self.state(), before)
        assert_equal(self.history_files(), disk_before)
        assert_equal(node.auditrdtsgrandfather(1), result)
        assert_equal(node.cli.auditrdtsgrandfather(1, tip, 0), result)
        assert_equal(node.auditrdtsgrandfather(tip, tip)['blocks_scanned'], 1)
        # Genesis has no undo record; it must be reported as incomplete.
        genesis = node.auditrdtsgrandfather(0, 0)
        assert_equal(genesis['missing_undo_data'], 1)
        assert_equal(genesis['complete'], False)
        assert_equal(self.state(), before)
        for args, message in (([-1, tip], 'start_height must be non-negative'),
                              ([2, 1], 'end_height must be at least start_height'),
                              ([1, tip + 1], 'end_height exceeds snapshot tip'),
                              ([1, tip, -1], 'max_failures must be between 0 and 10000'),
                              ([1, tip, 10001], 'max_failures must be between 0 and 10000')):
            assert_raises_rpc_error(-8, message, node.auditrdtsgrandfather, *args)
        # Regtest has no configured Purity activation before the tip.
        assert_raises_rpc_error(-8, 'end_height must be at least start_height', node.auditrdtsgrandfather)

        self.log.info('Pruned historical data must make the audit incomplete')
        for height in range(tip + 1, 451):
            coinbase = create_coinbase(height)
            coinbase.vout[0].scriptPubKey = CScript([OP_RETURN] + [OP_NOP] * 70000)
            coinbase.rehash()
            block = create_block(int(node.getbestblockhash(), 16), coinbase,
                                 node.getblockheader(node.getbestblockhash())['time'] + 1)
            block.solve()
            assert_equal(node.submitblock(block.serialize().hex()), None)
        node.pruneblockchain(150)
        before = self.state()
        disk_before = self.history_files()
        pruned = node.auditrdtsgrandfather(1, 450)
        assert pruned['missing_block_data'] > 0
        assert pruned['missing_undo_data'] > 0
        assert_equal(pruned['complete'], False)
        assert_equal(self.state(), before)
        assert_equal(self.history_files(), disk_before)


if __name__ == '__main__':
    AuditRdtsGrandfatherTest(__file__).main()
