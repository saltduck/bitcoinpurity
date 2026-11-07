#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""CLI regressions for Guix and legacy release manifest artifacts."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).with_name("generate-releases-manifest.py")
REPO = "saltduck/bitcoinpurity"
RELEASED_AT = "2026-11-05T00:00:00Z"

# Asset names from https://github.com/saltduck/bitcoinpurity/releases/tag/v1.1.2.
RELEASE_ASSETS = (
    "bitcoinpurity-1.1.2-aarch64-linux-gnu.tar.gz",
    "bitcoinpurity-1.1.2-arm64-apple-darwin-unsigned.zip",
    "bitcoinpurity-1.1.2-win64-pgpverifiable.zip",
    "bitcoinpurity-1.1.2-win64-setup-pgpverifiable.exe",
    "bitcoinpurity-1.1.2-x86_64-apple-darwin-unsigned.zip",
    "bitcoinpurity-1.1.2-x86_64-linux-gnu.tar.gz",
    "bitcoinpurity-1.1.2.tar.gz",
    "SHA256SUMS",
)


class GenerateReleasesManifestTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.artifacts = self.root / "artifacts"
        self.artifacts.mkdir()
        self.output = self.root / "releases.json"

    def write_artifact(self, name):
        path = self.artifacts / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(name.encode())
        return path

    def run_script(self, version="v1.1.2", repo=REPO):
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--version", version,
             "--artifacts-dir", str(self.artifacts), "--repo", repo,
             "--released-at", RELEASED_AT, "--output", str(self.output)],
            capture_output=True, text=True,
        )

    def assert_manifest(self, result, expected, version="1.1.2", repo=REPO):
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads(self.output.read_text())
        artifacts = []
        for platform, name in sorted(expected.items(), key=lambda item: item[1]):
            contents = (self.artifacts / name).read_bytes()
            artifacts.append({
                "platform": platform,
                "download_uri": f"https://github.com/{repo}/releases/download/v{version}/{Path(name).name}",
                "archive_sha256": hashlib.sha256(contents).hexdigest(),
                "archive_size_bytes": len(contents),
            })
        self.assertEqual(manifest, {
            "schema": 1,
            "latest": {"version": version, "released_at": RELEASED_AT,
                       "urgency": "optional",
                       "release_notes_url": f"https://github.com/{repo}/releases/tag/v{version}",
                       "artifacts": artifacts},
        })

    def assert_rejected(self, result, message):
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(message, result.stderr)
        self.assertFalse(self.output.exists())

    def test_v1_1_2_release_assets(self):
        for name in RELEASE_ASSETS:
            self.write_artifact(name)
        self.assert_manifest(self.run_script(), {
            "linux-arm64": RELEASE_ASSETS[0],
            "macos-arm64": RELEASE_ASSETS[1],
            "windows-x86_64": RELEASE_ASSETS[2],
            "macos-x86_64": RELEASE_ASSETS[4],
            "linux-x86_64": RELEASE_ASSETS[5],
        })

    def test_macos_selects_application_zip_over_cli_tarball(self):
        for host, platform in (("arm64-apple-darwin", "macos-arm64"),
                               ("x86_64-apple-darwin", "macos-x86_64")):
            with self.subTest(host=host):
                name = f"bitcoinpurity-1.1.2-{host}.zip"
                self.write_artifact(name)
                self.write_artifact(f"bitcoinpurity-1.1.2-{host}.tar.gz")
                self.assert_manifest(self.run_script(), {platform: name})
                for path in self.artifacts.iterdir():
                    path.unlink()

    def test_windows_selects_zip_over_installers(self):
        name = "bitcoinpurity-1.1.2-win64-pgpverifiable.zip"
        for artifact in (name, "bitcoinpurity-1.1.2-win64-setup-pgpverifiable.exe",
                         "bitcoinpurity-1.1.2-win64-setup.exe",
                         "bitcoin-win64-setup-1.1.2-x86_64-w64-mingw32.exe"):
            self.write_artifact(artifact)
        self.assert_manifest(self.run_script(), {"windows-x86_64": name})

    def test_codesigned_zips_preferred_over_presigning_zips(self):
        expected = {}
        for host, platform, variant in (("arm64-apple-darwin", "macos-arm64", "unsigned"),
                                        ("x86_64-apple-darwin", "macos-x86_64", "unsigned"),
                                        ("win64", "windows-x86_64", "pgpverifiable")):
            name = f"bitcoinpurity-1.1.2-{host}.zip"
            self.write_artifact(f"{host}-codesigned/{name}")
            self.write_artifact(f"{host}/bitcoinpurity-1.1.2-{host}-{variant}.zip")
            expected[platform] = f"{host}-codesigned/{name}"
        self.assert_manifest(self.run_script(), expected)

    def test_auxiliary_and_unsupported_archives_are_excluded(self):
        name = "bitcoinpurity-1.1.2-x86_64-linux-gnu.tar.gz"
        self.write_artifact(name)
        for artifact in ("bitcoinpurity-1.1.2.tar.gz",
                         "bitcoinpurity-1.1.2-x86_64-linux-gnu-debug.tar.gz",
                         "bitcoinpurity-1.1.2-win64-debug.zip",
                         "bitcoinpurity-1.1.2-win64-codesigning.tar.gz",
                         "bitcoinpurity-1.1.2-arm64-apple-darwin-codesigning.tar.gz",
                         "bitcoinpurity-1.1.2-arm64-apple-darwin-unsigned.tar.gz",
                         "bitcoinpurity-1.1.2-arm64-apple-darwin-pgpverifiable.zip",
                         "bitcoinpurity-1.1.2-win64-unsigned.zip",
                         "bitcoinpurity-1.1.2-x86_64-linux-gnu-unsigned.tar.gz",
                         "bitcoinpurity-1.1.2-arm-linux-gnueabihf.tar.gz",
                         "bitcoinpurity-1.1.2-riscv64-linux-gnu.tar.gz",
                         "unrelated-x86_64-linux-gnu.tar.gz"):
            self.write_artifact(artifact)
        self.assert_manifest(self.run_script(), {"linux-x86_64": name})

    def test_legacy_archive_names(self):
        expected = {
            "linux-x86_64": "bitcoin-purity-1.1.2-x86_64-linux-gnu.tar.gz",
            "linux-arm64": "bitcoin-purity-1.1.2-aarch64-linux-gnu.tar.gz",
            "macos-arm64": "Bitcoin-Purity-1.1.2-arm64-apple-darwin.zip",
            "macos-x86_64": "bitcoin-purity-1.1.2-x86_64-apple-darwin.zip",
            "windows-x86_64": "bitcoin-purity-1.1.2-x86_64-w64-mingw32.zip",
        }
        for name in expected.values():
            self.write_artifact(name)
        self.assert_manifest(self.run_script(), expected)

    def test_filters_other_versions_before_zip_preference(self):
        name = "bitcoinpurity-1.1.2-win64-pgpverifiable.zip"
        self.write_artifact(name)
        self.write_artifact("bitcoinpurity-1.1.3-win64.zip")
        result = self.run_script()
        self.assert_manifest(result, {"windows-x86_64": name})
        self.assertIn("requested '1.1.2'", result.stderr)

    def test_rc_bare_version_and_custom_repo(self):
        name = "bitcoinpurity-1.2.0rc1-x86_64-linux-gnu.tar.gz"
        self.write_artifact(name)
        self.assert_manifest(self.run_script("1.2.0rc1", "example/purity"),
                             {"linux-x86_64": name}, "1.2.0rc1", "example/purity")

    def test_other_versions_reported_when_no_match(self):
        self.write_artifact("bitcoinpurity-1.1.3-x86_64-linux-gnu.tar.gz")
        result = self.run_script()
        self.assert_rejected(result, "no artifacts for version '1.1.2'")
        self.assertIn("--version 1.1.3", result.stderr)

    def test_equally_preferred_duplicates_rejected(self):
        for name in ("bitcoinpurity-1.1.2-x86_64-linux-gnu.tar.gz",
                     "bitcoinpurity-1.1.2-arm64-apple-darwin.zip",
                     "bitcoinpurity-1.1.2-arm64-apple-darwin-unsigned.zip",
                     "bitcoinpurity-1.1.2-win64.zip",
                     "bitcoinpurity-1.1.2-win64-pgpverifiable.zip"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                self.artifacts = Path(directory)
                self.write_artifact(f"first/{name}")
                self.write_artifact(f"second/{name}")
                self.assert_rejected(self.run_script(), "multiple artifacts for the same platform")

    def test_identical_release_upload_copies_merged(self):
        expected = {}
        for platform, host, name in (
            ("linux-arm64", "aarch64-linux-gnu", RELEASE_ASSETS[0]),
            ("macos-arm64", "arm64-apple-darwin", RELEASE_ASSETS[1]),
            ("windows-x86_64", "x86_64-w64-mingw32", RELEASE_ASSETS[2]),
            ("macos-x86_64", "x86_64-apple-darwin", RELEASE_ASSETS[4]),
            ("linux-x86_64", "x86_64-linux-gnu", RELEASE_ASSETS[5]),
        ):
            original = f"{host}/{name}"
            upload = f"release-upload-1.1.2/{name}"
            path = self.write_artifact(original)
            self.write_artifact(upload).write_bytes(path.read_bytes())
            expected[platform] = min(original, upload)
        self.assert_manifest(self.run_script(), expected)

    def test_same_name_and_size_with_different_hash_rejected(self):
        name = "bitcoinpurity-1.1.2-x86_64-linux-gnu.tar.gz"
        self.write_artifact(f"x86_64-linux-gnu/{name}").write_bytes(b"original")
        self.write_artifact(f"release-upload-1.1.2/{name}").write_bytes(b"modified")
        self.assert_rejected(self.run_script(), "multiple artifacts for the same platform")

    def test_same_content_with_different_asset_names_rejected(self):
        for prefix in ("bitcoinpurity", "bitcoin-purity"):
            self.write_artifact(f"{prefix}-1.1.2-win64.zip").write_bytes(b"identical")
        self.assert_rejected(self.run_script(), "multiple artifacts for the same platform")

    def test_only_cli_tarballs_and_installers_produce_no_manifest(self):
        for name in ("bitcoinpurity-1.1.2-arm64-apple-darwin.tar.gz",
                     "bitcoinpurity-1.1.2-win64-setup.exe"):
            self.write_artifact(name)
        self.assert_rejected(self.run_script(), "no release artifacts found")

    def test_empty_and_missing_artifact_directories(self):
        self.assert_rejected(self.run_script(), "no release artifacts found")
        self.artifacts = self.root / "missing"
        self.assert_rejected(self.run_script(), "artifacts directory not found")

    def test_default_artifacts_directory_uses_guix_version_in_working_directory(self):
        for version, bare_version in (("v1.1.2", "1.1.2"), ("1.1.2", "1.1.2"),
                                      ("v1.2.0rc1", "1.2.0rc1")):
            with self.subTest(version=version):
                self.artifacts = self.root / f"guix-build-{bare_version}" / "output"
                name = f"x86_64-linux-gnu/bitcoinpurity-{bare_version}-x86_64-linux-gnu.tar.gz"
                self.write_artifact(name)
                result = subprocess.run(
                    [sys.executable, str(SCRIPT), "--version", version,
                     "--released-at", RELEASED_AT, "--output", str(self.output)],
                    cwd=self.root, capture_output=True, text=True,
                )
                self.assert_manifest(result, {"linux-x86_64": name}, version=bare_version)


if __name__ == "__main__":
    unittest.main()
