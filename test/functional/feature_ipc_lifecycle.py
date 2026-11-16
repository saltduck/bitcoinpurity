#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license.
"""A connected IPC peer must not prevent node shutdown or restart."""

from pathlib import Path
import socket
import tempfile

from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import assert_equal


class IpcLifecycleTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def skip_test_if_missing_module(self):
        self.binary = Path(self.config["environment"]["BUILDDIR"]) / "bin" / "bitcoin-node"
        if not hasattr(socket, "AF_UNIX") or not self.binary.exists():
            raise SkipTest("Unix multiprocess node required")

    def setup_nodes(self):
        self.ipc_dir = tempfile.TemporaryDirectory(prefix="purity-ipc-", dir="/tmp")
        self.ipc_path = Path(self.ipc_dir.name) / "node.sock"
        self.add_nodes(1, extra_args=[[f"-ipcbind=unix:{self.ipc_path}"]], binary=[str(self.binary)])
        self.start_nodes()

    def run_test(self):
        node = self.nodes[0]
        try:
            for reindex in (False, True):
                if reindex:
                    self.start_node(0, extra_args=[f"-ipcbind=unix:{self.ipc_path}", "-reindex-chainstate"])
                assert self.ipc_path.is_socket()
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as peer:
                    peer.settimeout(5)
                    with node.assert_debug_log(["IPC server: socket connected."]):
                        peer.connect(str(self.ipc_path))
                    node.stop_node(wait_until_stopped=False)
                    node.wait_until_stopped(timeout=10)
                    assert_equal(peer.recv(1), b"")
        finally:
            self.ipc_dir.cleanup()


if __name__ == "__main__":
    IpcLifecycleTest(__file__).main()
