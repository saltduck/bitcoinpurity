#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Package and execute a Mach-O fixture with relative transitive dependencies."""

import os
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == "darwin", "requires native Mach-O tooling")
class MacDeployTest(unittest.TestCase):
    def test_relative_transitive_libraries(self):
        for install_name in ("@rpath/libchild.dylib", "@loader_path/../deps/libchild.dylib"):
            with self.subTest(install_name=install_name), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                libraries = root / "lib"
                dependencies = root / "deps"
                libraries.mkdir()
                dependencies.mkdir()
                app = root / "Input.app"
                executable = app / "Contents/MacOS/Bitcoin-Qt"
                executable.parent.mkdir(parents=True)
                (app / "Contents/Resources").mkdir()
                with (app / "Contents/Info.plist").open("wb") as info:
                    plistlib.dump({"CFBundleExecutable": "Bitcoin-Qt",
                                   "CFBundleIdentifier": "org.bitcoinpurity.deploy-test",
                                   "CFBundlePackageType": "APPL"}, info)
                child = dependencies / "libchild.dylib"
                parent = libraries / "libparent.dylib"
                for filename, code in (("child.c", "int child(void) { return 42; }"),
                                       ("parent.c", "int child(void); int parent(void) { return child(); }"),
                                       ("main.c", "int parent(void); int main(void) { return parent() != 42; }")):
                    (root / filename).write_text(code)
                subprocess.run(["clang", "-dynamiclib", str(root / "child.c"),
                                "-install_name", install_name, "-o", str(child)], check=True)
                subprocess.run(["clang", "-dynamiclib", str(root / "parent.c"),
                                str(child), "-Wl,-rpath,@loader_path/../deps",
                                "-install_name", str(parent), "-o", str(parent)], check=True)
                subprocess.run(["clang", str(root / "main.c"), str(parent),
                                "-o", str(executable)], check=True)
                originals = {binary: binary.read_bytes() for binary in (child, parent, executable)}
                script = Path(__file__).with_name("macdeployqtplus")
                deployed = subprocess.run([sys.executable, str(script), str(app), "test",
                                           "-no-plugins", "-no-strip"], cwd=root,
                                          capture_output=True, text=True)
                self.assertEqual(deployed.returncode, 0, deployed.stdout + deployed.stderr)
                for binary, original in originals.items():
                    self.assertEqual(binary.read_bytes(), original)
                output = root / "dist/Bitcoin-Qt.app/Contents"
                for binary in (output / "MacOS/Bitcoin-Qt",
                               output / "Frameworks/libparent.dylib",
                               output / "Frameworks/libchild.dylib"):
                    linked = subprocess.check_output(["otool", "-L", str(binary)], encoding="utf8")
                    self.assertNotIn(str(libraries), linked)
                    self.assertNotIn(str(dependencies), linked)
                subprocess.run([str(output / "MacOS/Bitcoin-Qt")], check=True,
                               env={key: value for key, value in os.environ.items()
                                    if not key.startswith("DYLD_")})


if __name__ == "__main__":
    unittest.main()
