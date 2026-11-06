# TASK-030: Guix release manifest artifacts

Adapt `contrib/official-packages/generate-releases-manifest.py` to the asset
names in [v1.1.2](https://github.com/saltduck/bitcoinpurity/releases/tag/v1.1.2).
Preserve legacy archive names and the unsigned manifest schema. See the
[requirements](../../input/requirements.md),
[architecture](../architecture/official-packages.md#software-release-manifest-generator)
and [CLI contract](../api/official-packages.md#software-release-manifest-generator).

## Acceptance

- The eight v1.1.2 asset names yield five platform entries: two Linux tarballs,
  two macOS unsigned application ZIPs and one Windows pgpverifiable ZIP.
- macOS CLI tarballs and Windows installers never replace application ZIPs.
- Source/debug/codesigning and unsupported-platform archives are excluded.
- Final codesigned ZIPs take priority over presigning ZIPs for the requested
  version. Equally preferred copies with identical filenames, byte sizes and
  SHA256 digests are merged; conflicting artifacts fail.
- Legacy names, RC/bare versions, custom repository URLs, SHA256, byte sizes,
  unsigned schema 1 and errors for missing/mismatched input are tested.
- Omitted `--artifacts-dir` scans `./guix-build-<version>/output` relative to
  the working directory, using `--version` without the leading `v`; explicit
  artifact directories remain supported.

## Validation

Test-first: the new CLI suite reproduced the Guix name rejection and legacy
Windows installer selection before implementation (12 tests, 15 failures
including subtests). After implementation all 12 tests pass.

The default-directory regression first reproduced the old directory lookup,
then passed for prefixed, bare and RC versions with the derived Guix directory.
The upload-staging regression reproduced all five platform duplicates from
original Guix outputs plus identical `release-upload-1.1.2` copies before the
fix. Copies now yield one entry per platform; equal-size files with different
hashes and different asset names with identical contents are still rejected.
The release suite has 16 passing tests; combined with the node-data manifest
suite, all 42 tests pass.

```sh
python3 contrib/official-packages/test_generate_releases_manifest.py
```

Fixtures use the actual v1.1.2 asset names with small local byte payloads;
release binaries are not downloaded. No workflow change, commit, signing,
upload or deployment is included.
