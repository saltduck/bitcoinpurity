# Bitcoin Purity Software Updates

This document describes the signed `releases.json` manifest used by the
bitcoin-qt GUI update checker and the operational workflow for publishing it.

## Overview

The GUI periodically fetches:

```text
https://downloads.bitcoinpurity.org/releases.json
```

The manifest is signed with the same ECDSA scheme as official datadir package
manifests (`contrib/official-packages/sign-manifest.py`). The client verifies the
signature before comparing versions or downloading artifacts.

User flow:

1. Detect a newer signed release for the current platform.
2. Prompt the user in the GUI.
3. Download the archive to the system Downloads folder.
4. Verify `archive_sha256` from the manifest.
5. Show platform-specific manual install instructions.

The client does **not** replace installed binaries automatically.

## Manifest schema (`schema: 1`)

```json
{
  "schema": 1,
  "latest": {
    "version": "1.0.1",
    "released_at": "2026-09-02T00:00:00Z",
    "urgency": "optional",
    "release_notes_url": "https://github.com/saltduck/bitcoinpurity/releases/tag/v1.0.1",
    "artifacts": [
      {
        "platform": "linux-x86_64",
        "download_uri": "https://github.com/saltduck/bitcoinpurity/releases/download/v1.0.1/bitcoin-purity-1.0.1-x86_64-linux-gnu.tar.gz",
        "archive_sha256": "<hex>",
        "archive_size_bytes": 123456789
      }
    ]
  },
  "signature": "<base64>"
}
```

### Fields

| Field | Description |
|---|---|
| `version` | Bitcoin Purity release version (`MAJOR.MINOR.PATCH`, optional `rcN`) |
| `released_at` | ISO-8601 UTC timestamp |
| `urgency` | `optional`, `recommended`, or `required` |
| `release_notes_url` | Link shown to the user |
| `artifacts[].platform` | `linux-x86_64`, `linux-arm64`, `macos-arm64`, or `windows-x86_64` |
| `artifacts[].download_uri` | HTTPS URL (GitHub Releases or downloads.bitcoinpurity.org) |
| `artifacts[].archive_sha256` | SHA-256 of the release archive |
| `artifacts[].archive_size_bytes` | Expected file size in bytes |

## Publishing workflow

### 1. Generate the unsigned manifest

After GitHub Actions builds release archives, generate `releases.json`:

```bash
python3 contrib/official-packages/generate-releases-manifest.py \
  --version v1.0.1 \
  --artifacts-dir release-artifacts \
  --repo saltduck/bitcoinpurity \
  --output releases.json
```

The release workflow runs this step automatically when a `v*` tag is pushed.

### 2. Sign the manifest

```bash
python3 contrib/official-packages/sign-manifest.py \
  --sign releases.json \
  --key manifest-sign.pem
```

GitHub Actions signs automatically when the `RELEASES_MANIFEST_SIGNING_KEY`
repository secret contains the PEM private key.

### 3. Upload to the CDN

Publish the signed file to:

```text
https://downloads.bitcoinpurity.org/releases.json
```

The unsigned manifest is also attached to the GitHub Release for auditability.

## Key rotation

The embedded verification public key lives in
`src/kernel/software_updates.cpp` (`SOFTWARE_UPDATES_SIGNING_PUBKEY_HEX`).

To rotate keys:

1. Generate a new keypair: `./sign-manifest.py --generate-keypair releases-sign.pem`
2. Update the embedded public key in `software_updates.cpp`
3. Re-sign and publish `releases.json`
4. Ship a client release containing the new public key before retiring the old key

The current release uses the same key as official datadir package manifests.

## Client configuration

| Option | Default | Description |
|---|---|---|
| `-checkforupdates` | `1` | Enable automatic GUI update checks (`-nocheckforupdates` disables) |
| `-updatesmanifest=<url>` | CDN default | Override manifest URL (testing) |

Automatic checks are skipped for development builds (`-dev` suffix) and at most
once every seven days unless the user chooses **Help → Check for Updates…**.

## Security model

1. Manifest must be fetched from `downloads.bitcoinpurity.org` (or a local file
   when using `LOCAL` trust policy in tests).
2. Manifest signature is verified with the embedded secp256k1 public key.
3. Artifact downloads must use HTTPS and an allowed host (`github.com`,
   `release-assets.githubusercontent.com`, `objects.githubusercontent.com`, or
   `downloads.bitcoinpurity.org`).
4. Downloaded archives are verified against `archive_sha256` from the signed
   manifest before the user is prompted to install manually.
