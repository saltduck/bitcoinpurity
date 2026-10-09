#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise UTXO scans while blocks advance and flush, with and without an index."""

from concurrent.futures import ThreadPoolExecutor
from threading import Barrier

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, get_rpc_proxy, rpc_url


class UTXOStatsRaceTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.supports_cli = False
        self.extra_args = [['-coinstatsindex=0'], ['-coinstatsindex=0', '-prune=1', '-fastprune']]

    def concurrent_stats(self, use_index):
        node = self.nodes[0]
        modes = ['hash_serialized_3', 'muhash', 'none']
        barrier = Barrier(4, timeout=60)
        address = node.get_deterministic_priv_key().address

        def worker(mode):
            rpc = get_rpc_proxy(rpc_url(node.datadir_path, node.index, node.chain, node.rpchost), node.index)
            results = []
            try:
                for _ in range(40):
                    barrier.wait()
                    if mode is None:
                        rpc.generatetoaddress(1, address)
                    else:
                        stats = rpc.gettxoutsetinfo(mode, None, use_index)
                        assert_equal(stats['bestblock'], rpc.getblockhash(stats['height']))
                        assert_equal(stats['height'], rpc.getblockheader(stats['bestblock'])['height'])
                        assert_equal(stats['txouts'], stats['height'])
                        results.append(stats)
                    barrier.wait()
                return results
            except BaseException:
                barrier.abort()
                raise

        with ThreadPoolExecutor(max_workers=4) as executor:
            futures = [executor.submit(worker, mode) for mode in [None] + modes]
            results = [future.result() for future in futures]
        self.sync_blocks()
        assert_equal(node.getblockcount(), self.nodes[1].getblockcount())
        if use_index:
            for mode, scans in zip(modes[1:], results[2:]):
                for stats in scans:
                    # Every indexed result must use the parent of its own
                    # bestblock, even if the active chain advanced meanwhile.
                    assert_equal(stats, node.gettxoutsetinfo(mode, stats['bestblock']))

    def run_test(self):
        full, pruned = self.nodes
        self.generate(full, 700)
        self.log.info('Concurrent scans with no CoinStatsIndex, in all three hash modes')
        self.concurrent_stats(False)
        pruned.pruneblockchain(400)
        assert pruned.getblockchaininfo()['pruneheight'] > 0
        for mode in ['hash_serialized_3', 'muhash', 'none']:
            a = full.gettxoutsetinfo(mode, None, False)
            b = pruned.gettxoutsetinfo(mode, None, False)
            del a['disk_size'], b['disk_size']
            assert_equal(a, b)
        for node in self.nodes:
            assert_equal(node.getindexinfo(), {})
            assert not (node.chain_path / 'indexes' / 'coinstatsindex').exists()

        self.log.info('Concurrent queries with CoinStatsIndex enabled')
        self.restart_node(0, extra_args=['-coinstatsindex=1'])
        self.connect_nodes(0, 1)
        self.wait_until(lambda: full.getindexinfo()['coinstatsindex']['synced'])
        self.concurrent_stats(True)
        for mode in ['muhash', 'none']:
            indexed = full.gettxoutsetinfo(mode)
            assert_equal(indexed, full.gettxoutsetinfo(mode, indexed['height']))
            assert_equal(indexed, full.gettxoutsetinfo(mode, indexed['bestblock']))
            scanned = full.gettxoutsetinfo(mode, None, False)
            for field, value in indexed.items():
                if field not in ['block_info', 'total_unspendable_amount']:
                    assert_equal(value, scanned[field])


if __name__ == '__main__':
    UTXOStatsRaceTest(__file__).main()
