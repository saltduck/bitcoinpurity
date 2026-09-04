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
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

PLATFORM_SUFFIXES = {
    "x86_64-linux-gnu": "linux-x86_64",
    "aarch64-linux-gnu": "linux-arm64",
    "arm64-apple-darwin": "macos-arm64",
    "x86_64-apple-darwin": "macos-x86_64",
    # Prefer the full mingw triplet before the shorter "win64" token.
    "x86_64-w64-mingw32": "windows-x86_64",
    "win64": "windows-x86_64",
}

PLATFORM_TOKEN = "|".join(re.escape(suffix) for suffix in PLATFORM_SUFFIXES)

ARCHIVE_EXTENSIONS = (".tar.gz", ".zip", ".exe")

# bitcoin-purity-1.0.0rc1-arm64-apple-darwin.zip
# bitcoin-purity-1.0.1-x86_64-linux-gnu.tar.gz
# bitcoin-purity-1.0.0rc2-win64.zip
ARTIFACT_WITH_PLATFORM = re.compile(
    rf"(?i)^bitcoin-purity-(.+?)-(?:{PLATFORM_TOKEN})\.(?:tar\.gz|zip|exe)$"
)

# cmake deploy / NSIS: bitcoin-win64-setup-1.0.0rc1-x86_64-w64-mingw32.exe
ARTIFACT_WIN64_SETUP = re.compile(
    r"(?i)^bitcoin-win64-setup-(.+)-(?:x86_64|i686)-w64-mingw32\.exe$"
)

# Bitcoin-Purity-1.0.0rc1.zip (macdeploy without host triplet)
ARTIFACT_MACDEPLOY = re.compile(r"(?i)^bitcoin-purity-(.+)\.(?:tar\.gz|zip)$")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_release_archive(filename: str) -> bool:
    lowered = filename.lower()
    return any(lowered.endswith(ext) for ext in ARCHIVE_EXTENSIONS)


def detect_platform(filename: str) -> str | None:
    lowered = filename.lower()
    if ARTIFACT_WIN64_SETUP.match(filename):
        return "windows-x86_64"
    for suffix, platform in PLATFORM_SUFFIXES.items():
        if suffix in lowered:
            return platform
    return None


def extract_version_from_artifact(filename: str) -> str | None:
    match = ARTIFACT_WITH_PLATFORM.match(filename)
    if match:
        return match.group(1)
    match = ARTIFACT_WIN64_SETUP.match(filename)
    if match:
        return match.group(1)
    match = ARTIFACT_MACDEPLOY.match(filename)
    if match:
        return match.group(1)
    return None


def normalize_tag(version: str) -> str:
    return version if version.startswith("v") else f"v{version}"


def normalize_bare_version(version: str) -> str:
    return normalize_tag(version).removeprefix("v")


def iter_artifact_paths(artifacts_dir: Path) -> list[Path]:
    paths: list[Path] = []
    for path in sorted(artifacts_dir.rglob("*")):
        if not path.is_file():
            continue
        if path.name in {"SHA256SUMS", "releases.json"}:
            continue
        if is_release_archive(path.name):
            paths.append(path)
    return paths


def select_artifacts_for_version(bare_version: str, artifact_paths: list[Path]) -> list[Path]:
    """Keep only artifacts whose embedded version matches --version."""
    selected: list[Path] = []
    other_versions: dict[str, list[str]] = {}

    for path in artifact_paths:
        artifact_version = extract_version_from_artifact(path.name)
        if artifact_version is None:
            print(
                f"warning: skipping artifact with unrecognized name: {path.name}",
                file=sys.stderr,
            )
            continue
        if artifact_version != bare_version:
            other_versions.setdefault(artifact_version, []).append(path.name)
            print(
                f"warning: skipping {path.name} (version {artifact_version!r}, "
                f"requested {bare_version!r})",
                file=sys.stderr,
            )
            continue
        selected.append(path)

    if selected:
        return selected

    message = [f"error: no artifacts for version {bare_version!r} in {artifact_paths[0].parent}"]
    if other_versions:
        details = "\n".join(
            f"  {version}: {', '.join(names)}"
            for version, names in sorted(other_versions.items())
        )
        message.append("Other versions present (skipped):")
        message.append(details)
        message.append(f"Re-run with one of: {', '.join(f'--version {v}' for v in sorted(other_versions))}")
    raise SystemExit("\n".join(message))


def ensure_unique_platforms(artifact_paths: list[Path]) -> None:
    by_platform: dict[str, list[str]] = {}
    for path in artifact_paths:
        platform = detect_platform(path.name)
        assert platform is not None
        by_platform.setdefault(platform, []).append(path.name)

    duplicates = {platform: names for platform, names in by_platform.items() if len(names) > 1}
    if duplicates:
        details = "\n".join(f"  {platform}: {', '.join(names)}" for platform, names in sorted(duplicates.items()))
        raise SystemExit(
            f"error: multiple artifacts for the same platform in version {artifact_paths[0].name!r}:\n"
            f"{details}\n"
            "Remove duplicates or narrow --artifacts-dir."
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="Release tag, e.g. v1.0.1 or 1.0.0rc1")
    parser.add_argument("--artifacts-dir", type=Path, required=True, help="Directory containing release archives")
    parser.add_argument("--repo", default="saltduck/bitcoinpurity", help="GitHub owner/repo for download URIs")
    parser.add_argument("--released-at", help="ISO-8601 UTC timestamp (default: now)")
    parser.add_argument("--urgency", default="optional", choices=["optional", "recommended", "required"])
    parser.add_argument("--output", type=Path, default=Path("releases.json"))
    args = parser.parse_args()

    if not args.artifacts_dir.is_dir():
        raise SystemExit(f"error: artifacts directory not found: {args.artifacts_dir}")

    bare_version = normalize_bare_version(args.version)
    tag = normalize_tag(bare_version)
    released_at = args.released_at or datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    release_notes_url = f"https://github.com/{args.repo}/releases/tag/{tag}"

    artifact_paths: list[Path] = []
    for path in iter_artifact_paths(args.artifacts_dir):
        platform = detect_platform(path.name)
        if platform is None:
            print(f"warning: skipping unrecognized artifact {path.name}", file=sys.stderr)
            continue
        artifact_paths.append(path)

    if not artifact_paths:
        raise SystemExit(f"error: no release artifacts found in {args.artifacts_dir}")

    artifact_paths = select_artifacts_for_version(bare_version, artifact_paths)
    ensure_unique_platforms(artifact_paths)

    artifacts: list[dict] = []
    for path in artifact_paths:
        platform = detect_platform(path.name)
        assert platform is not None
        artifacts.append({
            "platform": platform,
            "download_uri": f"https://github.com/{args.repo}/releases/download/{tag}/{path.name}",
            "archive_sha256": sha256_file(path),
            "archive_size_bytes": path.stat().st_size,
        })

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
