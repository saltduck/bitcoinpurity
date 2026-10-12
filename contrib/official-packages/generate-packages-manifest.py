#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Generate unsigned official-packages-mainnet.json from a node-data ZIP.

Reads bitcoinpurity-package.json at the detected data root without extracting
node data. Supports a common enclosing directory around blocks/ and chainstate/.
For older ZIPs without metadata, provide --id, --snapshot-height, --base-blockhash and
--prune-mib. Requires only Python 3's standard library.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
from urllib.parse import urlsplit
import zipfile


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate metadata key: {key}")
        result[key] = value
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True, help="Final node-data ZIP")
    parser.add_argument("--download-uri", required=True, help="Official HTTPS URL for the ZIP")
    parser.add_argument("--output", type=Path, default=Path("official-packages-mainnet.json"))
    parser.add_argument("--id", help="Package ID, required if missing from ZIP metadata")
    parser.add_argument("--snapshot-height", type=int, help="Actual snapshot height")
    parser.add_argument("--base-blockhash", help="Block hash at the snapshot height")
    parser.add_argument("--prune-mib", type=int, help="0 for full node; at least 550 for pruning")
    args = parser.parse_args()

    try:
        if args.output.exists() or args.output.is_symlink():
            raise ValueError(f"output already exists: {args.output}")
        uri = urlsplit(args.download_uri)
        if (not args.download_uri.startswith("https://") or uri.hostname != "downloads.bitcoinpurity.org"
                or uri.username is not None or uri.password is not None
                or uri.port not in (None, 443) or uri.path in ("", "/")
                or "@" in args.download_uri or uri.fragment
                or any(c.isspace() for c in args.download_uri)):
            raise ValueError("download URI must be an HTTPS file URL on downloads.bitcoinpurity.org")

        print(f"Reading ZIP metadata: {args.archive}", flush=True)
        with args.archive.open("rb") as handle:
            with zipfile.ZipFile(handle) as archive:
                members = {}
                for info in archive.infolist():
                    name = info.filename.replace("\\", "/")
                    parts = name.rstrip("/").split("/")
                    if (name.startswith("/") or re.match(r"^[A-Za-z]:", name)
                            or any(part in ("", ".", "..") for part in parts)):
                        raise ValueError(f"unsafe ZIP entry: {info.filename}")
                    if name in members:
                        raise ValueError(f"duplicate ZIP entry: {name}")
                    if info.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                        raise ValueError(f"unsupported ZIP compression: {name}")
                    if info.flag_bits & 1:
                        raise ValueError(f"encrypted ZIP entry: {name}")
                    members[name] = info

                prefixes = set()
                for name in members:
                    if name.startswith("__MACOSX/") or any(part.startswith("._") for part in name.split("/")):
                        continue
                    for marker in ("blocks/", "chainstate/"):
                        if name.startswith(marker):
                            prefixes.add("")
                        else:
                            pos = name.find("/" + marker)
                            if pos != -1:
                                prefixes.add(name[:pos + 1])
                if len(prefixes) > 1:
                    raise ValueError("ambiguous ZIP data roots: blocks/ and chainstate/ must share one prefix")
                prefix = next(iter(prefixes), "")
                if not any(name.startswith(prefix + "blocks/") and not info.is_dir()
                           for name, info in members.items()):
                    raise ValueError("ZIP is missing blocks/ data")
                current = prefix + "chainstate/CURRENT"
                if current not in members or members[current].is_dir():
                    raise ValueError("ZIP is missing chainstate/CURRENT")
                print(f"Detected data root: {prefix or '(ZIP root)'}", flush=True)

                metadata = {}
                metadata_path = prefix + "bitcoinpurity-package.json"
                if metadata_path in members:
                    try:
                        metadata = json.loads(archive.read(members[metadata_path]),
                                              object_pairs_hook=unique_object)
                    except (ValueError, UnicodeError) as error:
                        raise ValueError(f"invalid ZIP metadata: {error}") from error
                    if not isinstance(metadata, dict):
                        raise ValueError("invalid ZIP metadata: expected an object")
                extracted_size = sum(info.file_size for info in members.values() if not info.is_dir())

            package = {}
            for field in ("snapshot_height", "base_blockhash", "prune_mib", "id"):
                override = getattr(args, field)
                if override is not None and field in metadata and override != metadata[field]:
                    raise ValueError(f"--{field.replace('_', '-')} conflicts with ZIP metadata")
                value = override if override is not None else metadata.get(field)
                if value is None:
                    raise ValueError(f"missing {field}: supply --{field.replace('_', '-')}")
                package[field] = value

            if not isinstance(package["id"], str) or not package["id"].strip():
                raise ValueError("invalid id: expected a nonempty string")
            if type(package["snapshot_height"]) is not int or not 0 < package["snapshot_height"] <= 2147483647:
                raise ValueError("invalid snapshot_height: expected a positive 32-bit integer")
            blockhash = package["base_blockhash"]
            if not isinstance(blockhash, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", blockhash) or int(blockhash, 16) == 0:
                raise ValueError("invalid base_blockhash: expected a nonzero 64-digit hexadecimal hash")
            prune = package["prune_mib"]
            if type(prune) is not int or not (prune == 0 or 550 <= prune <= 2147483647):
                raise ValueError("invalid prune_mib: expected 0 or an integer from 550 to 2147483647")

            archive_size = handle.seek(0, 2)
            handle.seek(0)
            print(f"Computing SHA256 ({archive_size} bytes)...", flush=True)
            digest = hashlib.sha256()
            for chunk in iter(lambda: handle.read(1 << 20), b""):
                digest.update(chunk)

        package.update(download_uri=args.download_uri, archive_sha256=digest.hexdigest(),
                       archive_size_bytes=archive_size, extracted_size_bytes=extracted_size)
        with args.output.open("x", encoding="utf-8") as output:
            output.write(json.dumps({"packages": [package]}, indent=2) + "\n")
        print(f"Wrote unsigned manifest to {args.output}")
        return 0
    except zipfile.BadZipFile as error:
        print(f"error: invalid ZIP: {error}", file=sys.stderr)
        return 1
    except (OSError, ValueError, NotImplementedError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
