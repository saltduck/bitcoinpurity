# TASK-024: Unsigned mainnet package manifest generator

Status: implemented and locally verified; no upload or deployment.

Add `contrib/official-packages/generate-packages-manifest.py` to derive one
package entry from a node-data ZIP and calculate SHA256 and byte sizes. Output
only unsigned JSON with no private-key argument, signing step or OpenSSL
dependency, as requested by the user. Support data-root ZIP
metadata and explicit values for older metadata-free packages. Reject conflicts,
invalid metadata/archive layouts, disallowed URLs and existing output files.
Do not extract/modify node data, upload, or change the client/signing format.

Synchronize requirements, product specification, architecture, CLI contract,
migration report, contrib usage instructions and task index.

## Validation

Tests were added before implementation and initially failed because the new
generator did not exist. For the unsigned change, first updated regressions to
require generation without a key or external tools; both failed against the
previous mandatory `--key` implementation. Then removed key/signing code.
Standalone tests exercise unsigned generation, metadata-free packages,
overrides, metadata validation, archive/path validation, URL policy,
and preservation of existing outputs. All 26 tests passed, including
ZIP64 member support, optional configuration/directory size accounting, and
URL cases rejected by the existing client. Tests use small generated fixtures
without private keys or OpenSSL. They do not generate production manifests.
`git diff --check` passed; the task index and new documentation links resolve.

The initial implementation incorrectly required `blocks/` at the ZIP root.
Added failing wrapped-layout tests first, then detected a shared enclosing
prefix for data and metadata without modifying the client. Regressions cover
one/multiple wrapper levels, absent metadata, missing CURRENT, ambiguous roots
and macOS resource forks. A read-only check of the reported
`/Volumes/data/backup/BitcoinPurity-550-968190-658170a8.zip` confirmed the data root
`BitcoinPurity968190/`. The corrected script accepts that layout and reaches the
missing-snapshot-metadata check. No production archive hashing, signing or
upload was performed during that structural check.

```bash
python3 contrib/official-packages/test_generate_packages_manifest.py
git diff --check
```
