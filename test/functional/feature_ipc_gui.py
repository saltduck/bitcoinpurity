#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise GUI mining initialization and optional spawned-process IPC."""

import os
from pathlib import Path

from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import assert_equal
from test_framework.wallet import MiniWallet


class IpcGuiTest(BitcoinTestFramework):
    def add_options(self, parser):
        parser.add_argument("--gui-binary", type=Path)
        parser.add_argument("--gui-platform", default="windows" if os.name == "nt" else "offscreen")

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def skip_test_if_missing_module(self):
        self.gui = self.options.gui_binary or Path(self.config["environment"]["BUILDDIR"]) / "bin" / (
            "bitcoin-gui" + self.config["environment"]["EXEEXT"])
        if not self.gui.exists():
            if self.options.gui_binary:
                raise AssertionError(f"GUI binary not found: {self.gui}")
            raise SkipTest("multiprocess GUI has not been built")

    def setup_nodes(self):
        self.add_nodes(1, binary=[str(self.gui)])
        node = self.nodes[0]
        # GUI startup uses its sync wizard instead of the daemon's service mode.
        node.args.remove("-service-mode=1")
        (node.datadir_path / self.chain).mkdir()
        (node.datadir_path / self.chain / "settings.json").write_text('{"sync-mode":"full"}')
        runtime_dir = node.datadir_path / "qt-runtime"
        runtime_dir.mkdir(mode=0o700)
        # The minimal Qt backend cannot load application fonts on macOS.
        self.start_nodes([[f"-{self.chain}", "-splash=0", "-choosedatadir=0", "-disablewallet",
                           f"-guisettingsdir={node.datadir_path / 'gui-settings'}"]],
                         env={**os.environ, "QT_QPA_PLATFORM": self.options.gui_platform,
                              "XDG_RUNTIME_DIR": str(runtime_dir)})

    def run_test(self):
        node = self.nodes[0]
        assert_equal(node.echoipc("gui-ipc"), "gui-ipc")
        self.generate(MiniWallet(node), 1)
        template = node.getblocktemplate({"rules": ["segwit"]})
        assert_equal(template["height"], 2)
        assert_equal(template["previousblockhash"], node.getbestblockhash())


if __name__ == "__main__":
    IpcGuiTest(__file__).main()
