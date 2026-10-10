# Official datadir packages

Bitcoin Purity can bootstrap a new node from pre-built datadir archives published at
`https://downloads.bitcoinpurity.org/`. Each archive contains a pruned or full
`blocks/` and `chainstate/` tree built at a fixed snapshot height.

Package definitions (snapshot height, prune size, download URI, hashes, etc.) are
**not hardcoded in source**. The GUI fetches the current package list from:

- Mainnet: `https://downloads.bitcoinpurity.org/official-packages-mainnet.json`
- Testnet: `https://downloads.bitcoinpurity.org/official-packages-testnet.json`

This list is refreshed each time the first-run wizard loads the sync options, so
new packages can be published without shipping a new client build.

## Local overrides

For testing or air-gapped setups, use one of:

1. `-officialpackages=<path>` on the command line
2. `<datadir>/official-packages-<chain>.json` (optional per-node override)

Filenames tried in the datadir include `official-packages-main.json`,
`official-packages-mainnet.json` (mainnet alias), and `official-packages.json`.

Example local testing:

```bash
bitcoin-qt -officialpackages=/path/to/official-packages-mainnet.json
```

### JSON format

Remote manifests fetched from `downloads.bitcoinpurity.org` must include a
top-level detached ECDSA signature verified against the public key embedded in
`src/kernel/official_packages.cpp`. Sign local manifests for upload with:

```bash
contrib/official-packages/sign-manifest.py --sign official-packages-mainnet.json --key manifest-sign.pem
```

```json
{
  "packages": [
    {
      "id": "mainnet-910000-prune-2gb",
      "snapshot_height": 910000,
      "base_blockhash": "0000000000000000000108970acb9522ffd516eae17acddcb1bd16469194a821",
      "prune_mib": 1907,
      "download_uri": "https://downloads.bitcoinpurity.org/mainnet/mainnet-910000-prune-2gb.zip",
      "archive_sha256": "...",
      "archive_size_bytes": 8589934592,
      "extracted_size_bytes": 4294967296
    }
  ]
}
```

| Field | Description |
|---|---|
| `id` | Unique package identifier stored in settings |
| `snapshot_height` | Block height of the chainstate inside the archive |
| `base_blockhash` | Block hash at `snapshot_height` (validated after extraction) |
| `prune_mib` | `0` = full node; `>= 550` = automatic prune target in MiB |
| `download_uri` | Official download URL for the `.zip` archive |
| `archive_sha256` | SHA256 of the compressed archive (required, non-zero) |
| `archive_size_bytes` | Estimated download size (for UI disk-space hints) |
| `extracted_size_bytes` | Estimated size after extraction (for UI disk-space hints) |
| `signature` | Required for remote manifests: base64 ECDSA signature over SHA-256 of the JSON object with this field removed |

## Security

Remote package lists and archives are validated as follows:

1. The manifest must carry a valid detached ECDSA signature (STRICT policy).
2. Each `download_uri` must use HTTPS and point at `downloads.bitcoinpurity.org`.
3. Each package `snapshot_height` / `base_blockhash` must be internally
   consistent. If that height is already an assumeutxo or checkpoint pin, the
   hash must match; otherwise any mainnet height at or after Purity activation
   is accepted so new packages can be published without a client release.
4. Zip archives are scanned for path traversal (`..`, absolute paths) before
   extraction. Listing and extraction are implemented in-process with zlib
   inflate (no external `unzip`/`tar`), and extracted files must remain inside
   the destination datadir.

Local manifests loaded via `-officialpackages` or a datadir override skip
signature and download-host checks so developers can test offline.

## Package contents

Each `.zip` archive must contain:

```
blocks/
chainstate/
bitcoinpurity-package.json
```

Optionally include `bitcoin.conf` with package-specific defaults; it is extracted
into the datadir. Do **not** include runtime/user state such as `bitcoin_rw.conf`,
`settings.json`, `wallets/`, peer databases, or log files.

The manifest file documents the package identity and is validated on extraction:

```json
{
  "id": "mainnet-910000-prune-2gb",
  "snapshot_height": 910000,
  "base_blockhash": "0000000000000000000108970acb9522ffd516eae17acddcb1bd16469194a821",
  "prune_mib": 1907
}
```

## Building a package

1. Sync a node to the desired snapshot height with the target prune setting.
2. Stop the node cleanly.
3. From the network data directory, archive only the required paths:

```bash
DATADIR=~/.bitcoin
HEIGHT=910000
PACKAGE_ID=mainnet-910000-prune-2gb
PRUNE_MIB=1907
WORKDIR=$(mktemp -d)

cat > "${WORKDIR}/bitcoinpurity-package.json" <<EOF
{
  "id": "${PACKAGE_ID}",
  "snapshot_height": ${HEIGHT},
  "base_blockhash": "<block hash at height>",
  "prune_mib": ${PRUNE_MIB}
}
EOF

(cd "${DATADIR}" && zip -r "${WORKDIR}/${PACKAGE_ID}.zip" blocks chainstate)
(cd "${WORKDIR}" && zip -u "${PACKAGE_ID}.zip" bitcoinpurity-package.json)
# Optional: ship package defaults
# (cd "${DATADIR}" && zip -u "${WORKDIR}/${PACKAGE_ID}.zip" bitcoin.conf)
```

4. Compute the archive SHA256:

```bash
sha256sum "${WORKDIR}/${PACKAGE_ID}.zip"
```

5. Upload the archive to the URI referenced in the JSON config.

6. Add or update the package entry in the remote `official-packages-<chain>.json`
   hosted at `https://downloads.bitcoinpurity.org/`. No client recompile is required.

## Generating an unsigned mainnet catalog

Use Python 3 to generate an unsigned single-package catalog directly from a
finished node-data ZIP, without extracting it:

```bash
python3 contrib/official-packages/generate-packages-manifest.py \
  --archive /path/to/mainnet-node-data.zip \
  --download-uri https://downloads.bitcoinpurity.org/nodedata/mainnet-node-data.zip \
  --output /path/to/official-packages-mainnet.json
```

The script detects the common data root containing `blocks/` and `chainstate/`,
either directly in the ZIP or inside an enclosing directory such as
`BitcoinPurity968190/`. Multiple enclosing levels are supported; inconsistent
or multiple data roots are rejected. macOS resource-fork entries are ignored
when detecting the root. It reads `id`, `snapshot_height`, `base_blockhash` and
`prune_mib` from `bitcoinpurity-package.json` at that root, prints the detected
root, streams SHA256 over the entire ZIP,
records its byte size, and sums the uncompressed sizes of all file entries,
including package metadata and optional configuration. Directory entries do
not contribute to the extracted size. ZIP64 archives are supported.

Published older ZIPs may lack the metadata file. In that case, append all four
values to the command above (retain a trailing backslash on the preceding line):

```bash
  --id mainnet-961814-prune-5500mb \
  --snapshot-height 961814 \
  --base-blockhash 00000000000000040440db37ab428b029ee5dda57d192088c13046d124eeb80b \
  --prune-mib 5500
```

Use the actual values of your package; the numbers above are examples. Values
provided on the CLI must match any corresponding fields present in the ZIP.
The tool checks metadata types, unique JSON keys, safe/unique ZIP paths,
supported compression, `blocks/` data and `chainstate/CURRENT`. It trusts the
snapshot metadata; it does not read LevelDB to determine height/hash, validate
the chainstate, or establish that an archive is mainnet. Build and verify the
mainnet package separately before publication.

Output contains `packages` without a top-level `signature`. The generator
accepts no `--key` argument and uses only Python's standard library. It never
signs or uploads files, changes the ZIP, or replaces an existing output. Without
`--output`, it creates `official-packages-mainnet.json` in the current directory;
output directories must already exist. The separate signing procedure and
remote signature requirements described above remain unchanged.

Run the standalone regressions (no node build required):

```bash
python3 contrib/official-packages/test_generate_packages_manifest.py
```

## Download behaviour

The GUI downloader uses resumable HTTP downloads when the server supports
`Accept-Ranges: bytes`. Partial data is stored under `<datadir>/.package-download/`
and can be resumed after cancellation or restart; metadata is saved in a sidecar
`*.download.json` file next to the archive.

## Supported combinations

The GUI intro wizard only exposes packages listed in the fetched JSON configuration.
Each entry is a fixed combination of snapshot height and storage mode.

When adding a new package:

1. Build and verify the archive on a clean machine.
2. Update the remote `official-packages-<chain>.json` on downloads.bitcoinpurity.org.
3. Publish the archive to the matching download URI.

### Unambiguous signed JSON

Duplicate object keys are invalid recursively, including objects inside arrays
and decoded-equivalent keys such as `schema` and `\u0073chema`. The shared
manifest path rejects them before canonicalization, signature verification or
package consumption. This also applies to local fixtures so all consumers use
an unambiguous DOM. JSON-RPC parsing is unchanged. Remote package downloads
continue to require `https://downloads.bitcoinpurity.org`; this policy is not
expanded by the software updater's GitHub release allowlist. Archive SHA256
authenticity derives from the signed manifest.
