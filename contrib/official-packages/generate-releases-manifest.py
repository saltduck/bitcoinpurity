#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Generate an unsigned releases.json manifest for downloads.bitcoinpurity.org.

The manifest lists the latest Bitcoin Purity release and per-platform download
artifacts with SHA-256 digests. Sign the output with sign-manifest.py before
publishing to the CDN.

Usage:
  ./generate-releases-manifest.py \\
    --version v1.0.1 \\
    --artifacts-dir release-artifacts \\
    --repo saltduck/bitcoinpurity \\
    --output releases.json
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

PLATFORM_SUFFIXES = {
    "x86_64-linux-gnu": "linux-x86_64",
    "aarch64-linux-gnu": "linux-arm64",
    "arm64-apple-darwin": "macos-arm64",
    "win64": "windows-x86_64",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def detect_platform(filename: str) -> str | None:
    for suffix, platform in PLATFORM_SUFFIXES.items():
        if suffix in filename:
            return platform
    return None


def normalize_tag(version: str) -> str:
    return version if version.startswith("v") else f"v{version}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="Release tag, e.g. v1.0.1")
    parser.add_argument("--artifacts-dir", type=Path, required=True, help="Directory containing release archives")
    parser.add_argument("--repo", default="saltduck/bitcoinpurity", help="GitHub owner/repo for download URIs")
    parser.add_argument("--released-at", help="ISO-8601 UTC timestamp (default: now)")
    parser.add_argument("--urgency", default="optional", choices=["optional", "recommended", "required"])
    parser.add_argument("--output", type=Path, default=Path("releases.json"))
    args = parser.parse_args()

    if not args.artifacts_dir.is_dir():
        raise SystemExit(f"error: artifacts directory not found: {args.artifacts_dir}")

    tag = normalize_tag(args.version)
    bare_version = tag.removeprefix("v")
    released_at = args.released_at or datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    release_notes_url = f"https://github.com/{args.repo}/releases/tag/{tag}"

    artifacts: list[dict] = []
    for path in sorted(args.artifacts_dir.iterdir()):
        if not path.is_file():
            continue
        if path.name in {"SHA256SUMS", "releases.json"}:
            continue
        if not (path.name.endswith(".tar.gz") or path.name.endswith(".zip")):
            continue
        platform = detect_platform(path.name)
        if platform is None:
            print(f"warning: skipping unrecognized artifact {path.name}", file=sys.stderr)
            continue
        artifacts.append({
            "platform": platform,
            "download_uri": f"https://github.com/{args.repo}/releases/download/{tag}/{path.name}",
            "archive_sha256": sha256_file(path),
            "archive_size_bytes": path.stat().st_size,
        })

    if not artifacts:
        raise SystemExit(f"error: no release artifacts found in {args.artifacts_dir}")

    manifest = {
        "schema": 1,
        "latest": {
            "version": bare_version,
            "released_at": released_at,
            "urgency": args.urgency,
            "release_notes_url": release_notes_url,
            "artifacts": artifacts,
        },
    }

    args.output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote unsigned manifest to {args.output}")
    print(f"Sign with: {Path(__file__).with_name('sign-manifest.py')} --sign {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
