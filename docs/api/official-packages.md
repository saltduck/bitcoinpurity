# Official package generator CLI contract

`python3 contrib/official-packages/generate-packages-manifest.py` accepts:

| Argument | Contract |
| --- | --- |
| `--archive` | Required path to a finished node-data ZIP, including ZIP64. |
| `--download-uri` | Required file URL starting with `https://` on `downloads.bitcoinpurity.org`; no credentials, literal `@`, fragments or whitespace. Port absent or 443. |
| `--output` | Defaults to `official-packages-mainnet.json` in the current directory; parent must exist, destination must not exist. |
| `--id` | Nonempty package identifier. |
| `--snapshot-height` | Positive signed 32-bit integer. |
| `--base-blockhash` | Nonzero 64-digit hexadecimal block hash. |
| `--prune-mib` | Integer 0 for full node, or 550 through 2147483647 for pruning. |

The last four arguments are required only when their fields are absent from
`bitcoinpurity-package.json` at the detected data root. Overrides must match
fields present there. The archive must contain `blocks/` file data and
`chainstate/CURRENT`, either at the ZIP root or under a common enclosing
directory (including multiple wrapper levels). Inconsistent or multiple data
roots are rejected; macOS resource-fork entries do not affect detection.

Output is an unsigned JSON object with only `packages` containing one entry.
That entry contains the four metadata fields, `download_uri`, `archive_sha256`,
`archive_size_bytes` and `extracted_size_bytes`. The generator accepts no `--key`
argument, performs no signing and requires no OpenSSL installation. This tool
does not merge catalogs or upload files. Exit code 0 means an unsigned file was written; errors
return nonzero and go to stderr. See the contrib README for examples.

## Software release manifest generator

`python3 contrib/official-packages/generate-releases-manifest.py` requires
`--version` (with or without `v`). Optional arguments are `--artifacts-dir`
(default `./guix-build-<version>/output` relative to the current working directory,
using `--version` without the leading `v`),
`--repo` (default `saltduck/bitcoinpurity`), `--released-at` (default
current UTC), `--urgency` (`optional`, `recommended`, `required`; default
`optional`) and `--output` (default `releases.json`).

Accepted names use `bitcoinpurity-VERSION-HOST` or legacy
`bitcoin-purity-VERSION-HOST` (case insensitive):

| Host | Platform | Selected artifact |
| --- | --- | --- |
| `x86_64-linux-gnu` | `linux-x86_64` | `.tar.gz` |
| `aarch64-linux-gnu` | `linux-arm64` | `.tar.gz` |
| `arm64-apple-darwin` | `macos-arm64` | `.zip`, or `-unsigned.zip` |
| `x86_64-apple-darwin` | `macos-x86_64` | `.zip`, or `-unsigned.zip` |
| `win64`, `x86_64-w64-mingw32` | `windows-x86_64` | `.zip`, or `-pgpverifiable.zip` |

Only the requested version is considered. Unsuffixed final ZIPs take priority
over presigning ZIPs. Equally preferred copies in different subdirectories
are merged if filenames, byte sizes and SHA256 digests match; the first sorted
path is selected. Conflicting contents or different asset names still fail.
Other files (including EXE installers, macOS CLI
tarballs, source/debug/codesigning archives) are excluded. Empty input or no
matching artifacts fails without writing a manifest.

Output remains unsigned schema 1: `latest` contains version, UTC release time,
urgency, release notes URL and artifacts with platform, GitHub download URI,
SHA256 and byte size. Generation overwrites `--output` as before. Sign the final
manifest separately with `sign-manifest.py`.
