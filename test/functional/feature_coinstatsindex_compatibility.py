#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Check disabled/pruned startup, legacy coexistence, and ordinary RPC parity.

--legacy-bitcoind optionally exercises a real pre-backport Purity database and
an actual downgrade. Without it, an unreadable legacy DB must remain untouched.
All data directories belong to the functional test framework.
"""

import hashlib
from pathlib import Path

from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_node import ErrorMatch
from test_framework.util import assert_equal


class CoinStatsIndexCompatibilityTest(BitcoinTestFramework):
    def add_options(self, parser):
        parser.add_argument('--legacy-bitcoind', help='Pre-backport Purity bitcoind executable')

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.supports_cli = False
        self.extra_args = [[], ['-coinstatsindex=0', '-prune=1', '-fastprune']]

    def snapshot(self, path):
        return {str(p.relative_to(path)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in path.rglob('*') if p.is_file()}

    def run_test(self):
        node, pruned = self.nodes
        self.generate(node, 700)
        for n in self.nodes:
            assert_equal(n.getindexinfo(), {})
            assert not (n.chain_path / 'indexes' / 'coinstatsindex').exists()
            assert not (n.chain_path / 'indexes' / 'coinstats').exists()
        pruned.pruneblockchain(400)
        assert pruned.getblockchaininfo()['pruneheight'] > 0
        tip = node.getbestblockhash()
        scanned = node.gettxoutsetinfo('muhash')
        pruned_scanned = pruned.gettxoutsetinfo('muhash')
        del pruned_scanned['disk_size']
        del scanned['disk_size']
        self.restart_node(1, extra_args=self.extra_args[1])
        assert_equal(pruned.getbestblockhash(), tip)
        restarted_scan = pruned.gettxoutsetinfo('muhash')
        del restarted_scan['disk_size']
        assert_equal(restarted_scan, pruned_scanned)
        assert_equal(pruned.getindexinfo(), {})
        assert not (pruned.chain_path / 'indexes' / 'coinstatsindex').exists()

        self.stop_node(0)
        legacy_path = node.chain_path / 'indexes' / 'coinstats'
        new_path = node.chain_path / 'indexes' / 'coinstatsindex'
        current_binary = node.binary
        legacy_stats = None
        if self.options.legacy_bitcoind:
            node.args[node.args.index(node.binary)] = str(Path(self.options.legacy_bitcoind).resolve())
            node.binary = str(Path(self.options.legacy_bitcoind).resolve())
            self.start_node(0, extra_args=['-coinstatsindex=1'])
            self.wait_until(lambda: node.getindexinfo()['coinstatsindex']['synced'])
            legacy_stats = {h: node.gettxoutsetinfo('muhash', h) for h in [0, 1, 149, 150, 700]}
            self.stop_node(0)
            node.args[node.args.index(node.binary)] = current_binary
            node.binary = current_binary
        else:
            (legacy_path / 'db').mkdir(parents=True)
            (legacy_path / 'db' / 'CURRENT').write_bytes(b'unreadable legacy fixture\n')
        legacy_files = self.snapshot(legacy_path)
        assert legacy_files

        self.log.info('Default and explicit disabled startup ignore legacy data')
        for args in [[], ['-coinstatsindex=0']]:
            with node.assert_debug_log([], unexpected_msgs=[
                    'Old version of coinstatsindex found', 'coinstatsindex thread start',
                    'coinstatsindex is enabled']):
                self.start_node(0, extra_args=args)
                assert_equal(node.getbestblockhash(), tip)
                assert_equal(node.getindexinfo(), {})
                disabled_scan = node.gettxoutsetinfo('muhash')
                del disabled_scan['disk_size']
                assert_equal(disabled_scan, scanned)
                assert not new_path.exists()
                assert_equal(self.snapshot(legacy_path), legacy_files)
                self.stop_node(0)

        self.log.info('Enabled startup builds a separate new index and preserves legacy files')
        with node.assert_debug_log([f'Old version of coinstatsindex found at {legacy_path}']):
            self.start_node(0, extra_args=['-coinstatsindex=1'])
        self.wait_until(lambda: node.getindexinfo()['coinstatsindex']['synced'])
        assert (new_path / 'db').is_dir()
        assert_equal(self.snapshot(legacy_path), legacy_files)
        indexed = node.gettxoutsetinfo('muhash')
        assert_equal(indexed['bestblock'], tip)
        common = dict(indexed)
        del common['block_info'], common['total_unspendable_amount']
        scan_common = dict(scanned)
        del scan_common['transactions']
        assert_equal(common, scan_common)
        if legacy_stats:
            for height, expected in legacy_stats.items():
                assert_equal(node.gettxoutsetinfo('muhash', height), expected)

        with node.assert_debug_log(['coinstatsindex is enabled at height 700'],
                                   unexpected_msgs=['Syncing coinstatsindex with block chain']):
            self.restart_node(0, extra_args=['-coinstatsindex=1'])
        self.wait_until(lambda: node.getindexinfo()['coinstatsindex']['synced'])
        assert_equal(node.gettxoutsetinfo('muhash'), indexed)
        assert_equal(self.snapshot(legacy_path), legacy_files)
        if self.options.legacy_bitcoind:
            self.stop_node(0)
            new_files = self.snapshot(new_path)
            node.args[node.args.index(node.binary)] = str(Path(self.options.legacy_bitcoind).resolve())
            node.binary = str(Path(self.options.legacy_bitcoind).resolve())
            self.start_node(0, extra_args=['-coinstatsindex=1'])
            self.wait_until(lambda: node.getindexinfo()['coinstatsindex']['synced'])
            assert_equal(node.gettxoutsetinfo('muhash'), indexed)
            assert_equal(self.snapshot(new_path), new_files)
            self.stop_node(0)
            node.args[node.args.index(node.binary)] = current_binary
            node.binary = current_binary
            self.start_node(0, extra_args=['-coinstatsindex=1'])

        self.log.info('A fresh index cannot rebuild missing pruned history')
        self.stop_node(1)
        pruned.assert_start_raises_init_error(
            extra_args=['-coinstatsindex=1', '-prune=1', '-fastprune'],
            expected_msg='Index "coinstatsindex" needs block data that has been pruned',
            match=ErrorMatch.PARTIAL_REGEX,
        )
        self.start_node(1, extra_args=self.extra_args[1])
        assert_equal(pruned.getbestblockhash(), tip)
        assert_equal(pruned.getindexinfo(), {})


if __name__ == '__main__':
    CoinStatsIndexCompatibilityTest(__file__).main()
