#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""End-to-end tests for generating unsigned node-data package manifests."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import warnings
import zipfile

SCRIPT = Path(__file__).with_name("generate-packages-manifest.py")
HASH = "00000000000000040440db37ab428b029ee5dda57d192088c13046d124eeb80b"
METADATA = {"id": "mainnet-961814-prune-5500mb", "snapshot_height": 961814,
            "base_blockhash": HASH, "prune_mib": 5500}
URI = "https://downloads.bitcoinpurity.org/nodedata/node%20data.zip"


class GeneratePackagesManifestTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.archive = self.root / "node data.zip"
        self.output = self.root / "official-packages-mainnet.json"

    def write_zip(self, metadata=METADATA, extra=(), current=True, blocks=True, zip64=False, prefix=""):
        with zipfile.ZipFile(self.archive, "w", zipfile.ZIP_DEFLATED) as archive:
            if blocks:
                if zip64:
                    with archive.open(prefix + "blocks/blk00000.dat", "w", force_zip64=True) as entry:
                        entry.write(b"block fixture" * 50)
                else:
                    archive.writestr(prefix + "blocks/blk00000.dat", b"block fixture" * 50)
                archive.writestr(prefix + "blocks/index/000001.ldb", b"index fixture")
            if current:
                archive.writestr(prefix + "chainstate/CURRENT", b"MANIFEST-000001\n")
            archive.writestr(prefix + "chainstate/MANIFEST-000001", b"chainstate fixture")
            if metadata is not None:
                archive.writestr(prefix + "bitcoinpurity-package.json",
                                 metadata if isinstance(metadata, str) else json.dumps(metadata))
            with warnings.catch_warnings():
                warnings.simplefilter("ignore", UserWarning)
                for name, contents in extra:
                    archive.writestr(name, contents)

    def run_script(self, *args):
        return subprocess.run([sys.executable, str(SCRIPT), "--archive", str(self.archive),
                               "--download-uri", URI,
                               "--output", str(self.output), *args],
                              capture_output=True, text=True)

    def assert_rejected(self, result, message):
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(message, result.stderr)
        self.assertFalse(self.output.exists())

    def test_generate_unsigned_manifest(self):
        self.write_zip()
        original = self.archive.read_bytes()
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads(self.output.read_text())
        with zipfile.ZipFile(self.archive) as archive:
            extracted = sum(info.file_size for info in archive.infolist() if not info.is_dir())
        self.assertEqual(manifest["packages"], [{**METADATA, "download_uri": URI,
                         "archive_sha256": hashlib.sha256(original).hexdigest(),
                         "archive_size_bytes": len(original), "extracted_size_bytes": extracted}])
        self.assertEqual(set(manifest), {"packages"})
        self.assertEqual(self.archive.read_bytes(), original)
        self.assertEqual(sorted(p.name for p in self.root.iterdir()),
                         ["node data.zip", "official-packages-mainnet.json"])

    def test_missing_metadata_explicit_values(self):
        self.write_zip(metadata=None)
        result = self.run_script("--id", METADATA["id"], "--snapshot-height", "961814",
                                 "--base-blockhash", HASH, "--prune-mib", "5500")
        self.assertEqual(result.returncode, 0, result.stderr)
        package = json.loads(self.output.read_text())["packages"][0]
        for key, value in METADATA.items():
            self.assertEqual(package[key], value)

    def test_wrapped_package_metadata(self):
        for prefix in ("BitcoinPurity968190/", "backup/mainnet/BitcoinPurity968190/"):
            with self.subTest(prefix=prefix):
                self.write_zip(prefix=prefix)
                original = self.archive.read_bytes()
                result = self.run_script()
                try:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    package = json.loads(self.output.read_text())["packages"][0]
                    for key, value in METADATA.items():
                        self.assertEqual(package[key], value)
                    self.assertEqual(package["archive_sha256"], hashlib.sha256(original).hexdigest())
                    self.assertEqual(package["archive_size_bytes"], len(original))
                    self.assertEqual(self.archive.read_bytes(), original)
                finally:
                    self.output.unlink(missing_ok=True)

    def test_wrapped_package_without_metadata(self):
        self.write_zip(metadata=None, prefix="BitcoinPurity968190/")
        result = self.run_script("--id", "mainnet-968190-prune-550mb", "--snapshot-height", "968190",
                                 "--base-blockhash", HASH, "--prune-mib", "550")
        self.assertEqual(result.returncode, 0, result.stderr)
        package = json.loads(self.output.read_text())["packages"][0]
        self.assertEqual(package["snapshot_height"], 968190)
        self.assertEqual(package["prune_mib"], 550)

    def test_wrapped_package_requires_missing_metadata(self):
        self.write_zip(metadata=None, prefix="BitcoinPurity968190/")
        self.assert_rejected(self.run_script(), "snapshot_height")

    def test_ambiguous_package_roots(self):
        self.write_zip(prefix="first/", extra=[("second/blocks/blk00000.dat", b"blocks"),
                                               ("second/chainstate/CURRENT", b"current")])
        self.assert_rejected(self.run_script(), "ambiguous")

    def test_mac_metadata_does_not_change_root(self):
        self.write_zip(prefix="BitcoinPurity968190/",
                       extra=[("__MACOSX/BitcoinPurity968190/blocks/._blk00000.dat", b"metadata")])
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_wrapped_missing_chainstate_current(self):
        self.write_zip(prefix="BitcoinPurity968190/", current=False)
        self.assert_rejected(self.run_script(), "chainstate/CURRENT")

    def test_missing_metadata_requires_values(self):
        self.write_zip(metadata=None)
        self.assert_rejected(self.run_script(), "snapshot_height")

    def test_matching_overrides(self):
        self.write_zip()
        result = self.run_script("--id", METADATA["id"], "--prune-mib", "5500")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_conflicting_overrides(self):
        self.write_zip()
        self.assert_rejected(self.run_script("--snapshot-height", "961815"), "conflicts")

    def test_invalid_metadata(self):
        for changes in ({"snapshot_height": True}, {"snapshot_height": 0},
                        {"snapshot_height": 2147483648}, {"base_blockhash": "0" * 64},
                        {"base_blockhash": "invalid"}, {"prune_mib": 549},
                        {"prune_mib": -1}, {"prune_mib": True}, {"id": ""}):
            with self.subTest(changes=changes):
                self.write_zip(metadata={**METADATA, **changes})
                self.assert_rejected(self.run_script(), "invalid")

    def test_full_node_prune_target(self):
        self.write_zip(metadata={**METADATA, "prune_mib": 0})
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(self.output.read_text())["packages"][0]["prune_mib"], 0)

    def test_duplicate_json_keys(self):
        self.write_zip(metadata=json.dumps(METADATA)[:-1] + ', "snapshot_height": 961815}')
        self.assert_rejected(self.run_script(), "duplicate")

    def test_invalid_json(self):
        for metadata in ("[1]", "{broken}"):
            with self.subTest(metadata=metadata):
                self.write_zip(metadata=metadata)
                self.assert_rejected(self.run_script(), "metadata")

    def test_unsafe_or_duplicate_zip_entries(self):
        for name in ("../escape", "/absolute", "C:\\escape", "blocks/../escape",
                     "blocks/blk00000.dat", "bitcoinpurity-package.json"):
            with self.subTest(name=name):
                self.write_zip(extra=[(name, b"bad")])
                self.assert_rejected(self.run_script(), "ZIP")

    def test_missing_chainstate_current(self):
        self.write_zip(current=False)
        self.assert_rejected(self.run_script(), "chainstate/CURRENT")

    def test_missing_blocks(self):
        self.write_zip(blocks=False)
        self.assert_rejected(self.run_script(), "blocks/")

    def test_zip64_member(self):
        self.write_zip(zip64=True)
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        package = json.loads(self.output.read_text())["packages"][0]
        with zipfile.ZipFile(self.archive) as archive:
            self.assertEqual(archive.getinfo("blocks/blk00000.dat").extract_version, 45)
            self.assertEqual(package["extracted_size_bytes"], sum(i.file_size for i in archive.infolist()))
        self.assertEqual(package["archive_sha256"], hashlib.sha256(self.archive.read_bytes()).hexdigest())

    def test_unsupported_compression(self):
        self.write_zip()
        with zipfile.ZipFile(self.archive, "a", zipfile.ZIP_BZIP2) as archive:
            archive.writestr("blocks/unsupported.dat", b"unsupported")
        self.assert_rejected(self.run_script(), "compression")

    def test_directory_sizes_and_optional_config(self):
        self.write_zip(extra=[("blocks/", b""), ("chainstate/", b""), ("bitcoin.conf", b"prune=5500\n")])
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(self.archive) as archive:
            expected = sum(i.file_size for i in archive.infolist() if not i.is_dir())
        self.assertEqual(json.loads(self.output.read_text())["packages"][0]["extracted_size_bytes"], expected)

    def test_download_uri_policy(self):
        self.write_zip()
        for uri in ("http://downloads.bitcoinpurity.org/data.zip", "https://example.org/data.zip",
                    "https://user@downloads.bitcoinpurity.org/data.zip",
                    "https://downloads.bitcoinpurity.org", "HTTPS://downloads.bitcoinpurity.org/data.zip",
                    "https://downloads.bitcoinpurity.org/node@data.zip",
                    "https://downloads.bitcoinpurity.org:8443/data.zip",
                    "https://downloads.bitcoinpurity.org/data.zip#fragment",
                    "https://downloads.bitcoinpurity.org/node data.zip"):
            with self.subTest(uri=uri):
                try:
                    self.assert_rejected(self.run_script("--download-uri", uri), "download URI")
                finally:
                    self.output.unlink(missing_ok=True)

    def test_generation_without_external_tools(self):
        self.write_zip()
        result = subprocess.run([sys.executable, str(SCRIPT), "--archive", str(self.archive),
                                 "--download-uri", URI, "--output", str(self.output)],
                                env={**os.environ, "PATH": ""}, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("signature", json.loads(self.output.read_text()))

    def test_output_exists_is_preserved(self):
        self.write_zip()
        self.output.write_text("previous manifest")
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("already exists", result.stderr)
        self.assertEqual(self.output.read_text(), "previous manifest")

    def test_default_output_filename(self):
        self.write_zip()
        result = subprocess.run([sys.executable, str(SCRIPT), "--archive", str(self.archive),
                                 "--download-uri", URI],
                                cwd=self.root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(self.output.exists())

    def test_corrupt_archive(self):
        self.archive.write_bytes(b"not a ZIP")
        self.assert_rejected(self.run_script(), "ZIP")


if __name__ == "__main__":
    unittest.main()
