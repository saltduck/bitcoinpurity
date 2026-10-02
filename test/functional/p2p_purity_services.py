#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Purity service advertisement, legacy handshakes and address persistence."""

from test_framework.messages import (
    CAddress,
    NODE_NETWORK,
    NODE_PURITY_ASERT,
    NODE_REDUCED_DATA,
    NODE_WITNESS,
    msg_addr,
    msg_addrv2,
)
from test_framework.p2p import P2PInterface, p2p_lock
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class PurityServicesTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [["-externalip=250.1.1.1"], ["-prune=550"]]

    def setup_network(self):
        self.setup_nodes()

    def run_test(self):
        node = self.nodes[0]
        for n in self.nodes:
            info = n.getnetworkinfo()
            assert int(info["localservices"], 16) & NODE_PURITY_ASERT
            assert "PURITY_ASERT" in info["localservicesnames"]
            legacy = n.add_p2p_connection(P2PInterface(), services=NODE_NETWORK | NODE_WITNESS)
            assert legacy.nServices & NODE_PURITY_ASERT
            assert "PURITY_ASERT" not in n.getpeerinfo()[0]["servicesnames"]
            legacy.sync_with_ping()
            n.disconnect_p2ps()

        self.generate(node, 1, sync_fun=self.no_op)
        for addrv2 in (False, True):
            peer = node.add_p2p_connection(P2PInterface(support_addrv2=addrv2),
                services=NODE_NETWORK | NODE_WITNESS | NODE_PURITY_ASERT | (1 << 63))
            names = node.getpeerinfo()[0]["servicesnames"]
            assert "PURITY_ASERT" in names
            assert "UNKNOWN[2^63]" in names
            command = "addrv2" if addrv2 else "addr"
            def local_advertised():
                with p2p_lock:
                    return any(a.ip == "250.1.1.1" and a.nServices & NODE_PURITY_ASERT
                               for a in peer.last_message.get(command, msg_addr()).addrs)
            self.wait_until(local_advertised)
            address = CAddress()
            address.ip = "251.2.2.2" if addrv2 else "252.3.3.3"
            address.port = 8333
            address.time = node.getblockheader(node.getbestblockhash())["time"]
            address.nServices = NODE_NETWORK | NODE_WITNESS | NODE_PURITY_ASERT
            message = msg_addrv2() if addrv2 else msg_addr()
            message.addrs = [address]
            peer.send_and_ping(message)
            stored = [a for a in node.getnodeaddresses(0) if a["address"] == address.ip]
            assert_equal(len(stored), 1)
            assert_equal(stored[0]["services"], address.nServices)
            node.disconnect_p2ps()

        self.restart_node(0)
        for address in node.getnodeaddresses(0):
            if address["address"] in ("251.2.2.2", "252.3.3.3"):
                assert address["services"] & NODE_PURITY_ASERT
        assert_equal(len([a for a in node.getnodeaddresses(0)
                          if a["address"] in ("251.2.2.2", "252.3.3.3")]), 2)

        legacy = node.add_outbound_p2p_connection(P2PInterface(), p2p_idx=0,
            services=NODE_NETWORK | NODE_WITNESS | NODE_REDUCED_DATA)
        legacy.sync_with_ping()
        assert "PURITY_ASERT" not in node.getpeerinfo()[0]["servicesnames"]
        node.disconnect_p2ps()
        node.add_outbound_p2p_connection(P2PInterface(), p2p_idx=0,
            connection_type="feeler", services=NODE_NETWORK | NODE_WITNESS)


if __name__ == "__main__":
    PurityServicesTest(__file__).main()
