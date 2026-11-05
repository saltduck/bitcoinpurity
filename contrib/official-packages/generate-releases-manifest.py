#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Generate an unsigned releases.json manifest for downloads.bitcoinpurity.org.

The manifest lists the latest Bitcoin Purity release and per-platform download
artifacts with SHA-256 digests. Sign the output with sign-manifest.py before
publishing to the CDN.
Linux uses binary tarballs; macOS uses application ZIPs and Windows uses ZIPs.
Guix unsigned/pgpverifiable ZIPs are supported, with final codesigned ZIPs
preferred when both are present. Source, debug and codesigning archives and
installers are excluded. Legacy bitcoin-purity archive names remain supported.
Identical copies with the same filename, size and SHA-256 are merged.
Artifacts default to ./guix-build-<version>/output relative to the current
working directory, with the leading v removed from --version.

Usage:
  ./generate-releases-manifest.py \\
    --version v1.1.2 \\
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
    "x86_64-w64-mingw32": "windows-x86_64",
    "win64": "windows-x86_64",
}

PLATFORM_TOKEN = "|".join(re.escape(suffix) for suffix in PLATFORM_SUFFIXES)

ARCHIVE_EXTENSIONS = (".tar.gz", ".zip")

ARTIFACT_WITH_PLATFORM = re.compile(
    rf"(?i)bitcoin-?purity-(?P<version>.+?)-(?P<host>{PLATFORM_TOKEN})"
    r"(?:-(?P<variant>unsigned|pgpverifiable))?\.(?P<extension>tar\.gz|zip)"
)


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
    match = ARTIFACT_WITH_PLATFORM.fullmatch(filename.lower())
    if match is None:
        return None
    platform = PLATFORM_SUFFIXES[match.group("host")]
    variant = match.group("variant")
    extension = match.group("extension")
    if platform.startswith("linux-"):
        return platform if extension == "tar.gz" and variant is None else None
    if platform.startswith("macos-"):
        return platform if extension == "zip" and variant in {None, "unsigned"} else None
    return platform if extension == "zip" and variant in {None, "pgpverifiable"} else None


def extract_version_from_artifact(filename: str) -> str | None:
    match = ARTIFACT_WITH_PLATFORM.fullmatch(filename)
    if match:
        return match.group("version")
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


def select_platform_artifacts(artifact_paths: list[Path]) -> list[Path]:
    by_platform: dict[str, list[Path]] = {}
    for path in artifact_paths:
        platform = detect_platform(path.name)
        assert platform is not None
        by_platform.setdefault(platform, []).append(path)

    for platform, paths in by_platform.items():
        final_paths = [path for path in paths if not path.name.lower().endswith(
            ("-unsigned.zip", "-pgpverifiable.zip"))]
        candidates = final_paths or paths
        if (len(candidates) > 1
                and len({path.name for path in candidates}) == 1
                and len({path.stat().st_size for path in candidates}) == 1
                and len({sha256_file(path) for path in candidates}) == 1):
            candidates = candidates[:1]
        by_platform[platform] = candidates

    duplicates = {platform: names for platform, names in by_platform.items() if len(names) > 1}
    if duplicates:
        details = "\n".join(f"  {platform}: {', '.join(str(path) for path in paths)}"
                            for platform, paths in sorted(duplicates.items()))
        raise SystemExit(
            f"error: multiple artifacts for the same platform in version "
            f"{extract_version_from_artifact(artifact_paths[0].name)!r}:\n"
            f"{details}\n"
            "Remove duplicates or narrow --artifacts-dir."
        )
    selected = {paths[0] for paths in by_platform.values()}
    return [path for path in artifact_paths if path in selected]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="Release tag, e.g. v1.0.1 or 1.0.0rc1")
    parser.add_argument("--artifacts-dir", type=Path,
                        help="Directory containing release archives (default: ./guix-build-<version>/output, without leading v)")
    parser.add_argument("--repo", default="saltduck/bitcoinpurity", help="GitHub owner/repo for download URIs")
    parser.add_argument("--released-at", help="ISO-8601 UTC timestamp (default: now)")
    parser.add_argument("--urgency", default="optional", choices=["optional", "recommended", "required"])
    parser.add_argument("--output", type=Path, default=Path("releases.json"))
    args = parser.parse_args()

    bare_version = normalize_bare_version(args.version)
    if args.artifacts_dir is None:
        args.artifacts_dir = Path(f"guix-build-{bare_version}") / "output"

    if not args.artifacts_dir.is_dir():
        raise SystemExit(f"error: artifacts directory not found: {args.artifacts_dir}")

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
    artifact_paths = select_platform_artifacts(artifact_paths)

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
