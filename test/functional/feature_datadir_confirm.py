#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""CLI first-use confirmation for a non-empty data directory."""

import json
import os
import pty
import select
import shutil
import signal
import subprocess
import time
from pathlib import Path

from test_framework.test_framework import BitcoinTestFramework


class DatadirConfirmTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.add_nodes(self.num_nodes, self.extra_args)

    def run_test(self):
        self.binary = self.nodes[0].binary
        self.root = Path(self.options.tmpdir) / "confirm"
        self.root.mkdir()
        self.test_decline_and_accept()
        self.test_daemon_prompts_before_fork()
        self.test_noninteractive_and_service()
        self.test_conf_datadir_and_ignored_service_mode()
        self.test_move_keeps_approval()
        self.test_nosettings_and_corrupt_file()
        self.test_help_and_version_skip_confirmation()

    def _prepare(self, name):
        base = self.root / name
        if base.exists():
            shutil.rmtree(base)
        net = base / "regtest"
        net.mkdir(parents=True)
        (net / "keep").write_text("keep")
        blocker = base / "not-a-directory"
        blocker.write_text("x")
        return base, net, blocker

    def _env(self, **extra):
        env = os.environ.copy()
        env.pop("INVOCATION_ID", None)
        env.update(extra)
        return env

    def _cmd(self, base, blocker, *extra):
        return [
            self.binary,
            f"-datadir={base}",
            "-regtest",
            "-listen=0",
            f"-blocksdir={blocker}",
            *extra,
        ]

    def _run(self, cmd, env=None):
        return subprocess.run(
            cmd,
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            timeout=60,
            env=env or self._env(),
        )

    def _run_tty(self, cmd, responses, env=None, close_after_prompt=False):
        master, slave = pty.openpty()
        proc = subprocess.Popen(cmd, stdin=slave, stdout=slave, stderr=slave, env=env or self._env())
        os.close(slave)
        output = b""
        pending = list(responses)
        prompted = False
        deadline = time.time() + 60

        def read_available():
            nonlocal output, prompted
            while True:
                ready, _, _ = select.select([master], [], [], 0.2)
                if not ready:
                    return
                try:
                    chunk = os.read(master, 4096)
                except OSError:
                    return
                if not chunk:
                    return
                output += chunk
                if not prompted and b"[y/N]:" in output:
                    prompted = True
                    if pending:
                        os.write(master, pending.pop(0))
                    elif close_after_prompt:
                        return

        try:
            while time.time() < deadline:
                read_available()
                if prompted and close_after_prompt:
                    break
                if proc.poll() is not None:
                    read_available()
                    break
            if close_after_prompt:
                os.close(master)
                master = None
            returncode = proc.wait(timeout=60)
            if master is not None:
                read_available()
        finally:
            if master is not None:
                os.close(master)
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=10)
        return returncode, output.decode(errors="replace")

    def _marker(self, net):
        path = net / "settings.json"
        if not path.exists():
            return None
        return json.loads(path.read_text()).get("purity-cli-datadir")

    def test_decline_and_accept(self):
        self.log.info("Manual confirmation accepts only an explicit y")
        base, net, blocker = self._prepare("manual")
        code, output = self._run_tty(self._cmd(base, blocker), [b"n\n"])
        assert code != 0
        assert "not accepted" in output
        assert "Specified blocks directory" not in output
        assert self._marker(net) is None
        assert (net / "keep").read_text() == "keep"

        for answer in (b"\n", b"nope\n"):
            code, output = self._run_tty(self._cmd(base, blocker), [answer])
            assert code != 0
            assert "not accepted" in output
            assert self._marker(net) is None

        code, output = self._run_tty(self._cmd(base, blocker), [], close_after_prompt=True)
        assert code != 0
        # Closing the terminal delivers EOF. The message is covered by the unit
        # test; here, confirm the node never reached initialization.
        assert not (net / "settings.json").exists()
        assert not (net / "debug.log").exists()
        assert "Specified blocks directory" not in output

        code, output = self._run_tty(self._cmd(base, blocker), [b"y\n"])
        assert code != 0
        assert "Specified blocks directory" in output
        assert self._marker(net)["status"] == "approved"
        assert self._marker(net)["schema"] == 1

        code_again = self._run(self._cmd(base, blocker))
        assert "not been confirmed" not in code_again.stderr
        assert "Specified blocks directory" in code_again.stderr
        assert self._marker(net)["status"] == "approved"

    def test_daemon_prompts_before_fork(self):
        self.log.info("-daemon and -daemonwait prompt before forking")
        for flag in ("-daemon", "-daemonwait"):
            base, net, blocker = self._prepare(flag.strip("-"))
            code, output = self._run_tty(self._cmd(base, blocker, flag), [b"n\n"])
            assert code != 0, output
            assert "starting" not in output
            assert "not accepted" in output
            assert self._marker(net) is None

    def _run_service_until_logged(self, base, extra, env=None):
        cmd = [
            self.binary,
            f"-datadir={base}",
            "-regtest",
            "-listen=0",
            "-connect=0",
            f"-rpcport={18770 + len(extra)}",
            *extra,
        ]
        proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env or self._env())
        debug_path = base / "regtest" / "debug.log"
        debug = ""
        deadline = time.time() + 30
        try:
            while time.time() < deadline:
                if debug_path.exists():
                    debug = debug_path.read_text(errors="replace")
                    if "managed service" in debug:
                        break
                if proc.poll() is not None:
                    stderr = proc.stderr.read().decode(errors="replace")
                    raise AssertionError(f"service process exited early: {stderr}\n{debug}")
                time.sleep(0.1)
            else:
                raise AssertionError("service warning was not logged")
        finally:
            if proc.poll() is None:
                proc.send_signal(signal.SIGTERM)
                proc.wait(timeout=30)
        return debug

    def test_noninteractive_and_service(self):
        self.log.info("No TTY fails; systemd and -service-mode continue without approval")
        base, net, blocker = self._prepare("notty")
        result = self._run(self._cmd(base, blocker))
        assert result.returncode != 0
        assert "not been confirmed" in result.stderr
        assert "starting" not in result.stdout
        assert self._marker(net) is None

        base, net, _blocker = self._prepare("systemd")
        debug = self._run_service_until_logged(
            base,
            [],
            env=self._env(INVOCATION_ID="0123456789abcdef0123456789abcdef"),
        )
        assert "Approval was not recorded" in debug
        assert self._marker(net) is None

        base, net, _blocker = self._prepare("service-flag")
        self._run_service_until_logged(base, ["-service-mode=1"])
        assert self._marker(net) is None

        self.log.info("A later manual start still prompts when service mode did not approve")
        result = self._run(self._cmd(base, base / "not-a-directory"))
        assert "not been confirmed" in result.stderr

    def test_conf_datadir_and_ignored_service_mode(self):
        self.log.info("Confirmation uses datadir from bitcoin.conf and ignores service-mode there")
        base = self.root / "conf-base"
        target = self.root / "conf-target"
        shutil.rmtree(base, ignore_errors=True)
        shutil.rmtree(target, ignore_errors=True)
        net = target / "regtest"
        net.mkdir(parents=True)
        (net / "keep").write_text("keep")
        base.mkdir()
        (base / "bitcoin.conf").write_text(f"datadir={target}\nregtest=1\nservice-mode=1\n")
        result = self._run([self.binary, f"-conf={base / 'bitcoin.conf'}", "-listen=0"])
        assert result.returncode != 0
        assert "not been confirmed" in result.stderr
        assert str(net) in result.stderr
        assert "configuration file is ignored" in result.stderr
        assert self._marker(net) is None

    def test_move_keeps_approval(self):
        self.log.info("Moving a confirmed directory keeps its approval")
        base, net, blocker = self._prepare("move-src")
        code, output = self._run_tty(self._cmd(base, blocker), [b"y\n"])
        assert "Specified blocks directory" in output
        assert code != 0
        moved = self.root / "move-dst"
        shutil.move(base, moved)
        blocker = moved / "not-a-directory"
        result = self._run(self._cmd(moved, blocker))
        assert "not been confirmed" not in result.stderr
        assert "Specified blocks directory" in result.stderr
        assert self._marker(moved / "regtest")["status"] == "approved"

    def test_nosettings_and_corrupt_file(self):
        self.log.info("-nosettings does not create settings, and corrupt settings keep the old error")
        base, net, blocker = self._prepare("nosettings")
        code, output = self._run_tty(self._cmd(base, blocker, "-nosettings"), [b"y\n"])
        assert code != 0
        assert "Specified blocks directory" in output
        assert not (net / "settings.json").exists()
        again = self._run(self._cmd(base, blocker, "-nosettings"))
        assert "not been confirmed" in again.stderr

        base, net, blocker = self._prepare("corrupt")
        (net / "settings.json").write_text("not json")
        result = self._run(self._cmd(base, blocker))
        assert "does not contain valid JSON" in result.stderr
        assert "not been confirmed" not in result.stderr

    def test_help_and_version_skip_confirmation(self):
        self.log.info("-help and -version do not confirm a non-empty data directory")
        base, _net, _blocker = self._prepare("help")
        help_result = self._run([self.binary, f"-datadir={base}", "-regtest", "-help"])
        assert help_result.returncode == 0, help_result.stderr
        assert "Usage:" in help_result.stdout
        assert "[y/N]:" not in help_result.stderr
        assert "not been confirmed" not in help_result.stderr
        version_result = self._run([self.binary, f"-datadir={base}", "-regtest", "-version"])
        assert version_result.returncode == 0, version_result.stderr
        assert "version" in version_result.stdout.lower()
        assert "[y/N]:" not in version_result.stderr
        assert "not been confirmed" not in version_result.stderr


if __name__ == "__main__":
    DatadirConfirmTest(__file__).main()
