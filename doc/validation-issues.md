# Validation and signed-manifest corrections

Verified locally on 2026-10-06, based on master `09a69c0e56`. Scope is Issue 1
(deep-reorganization parking), Issue 2 (RDTS grandfathering), and Issue 4
(signed manifests). Replay protection is excluded. Original TASK-014 implementation commits:
`3051df401f` (parking), `edf5362ea9` (RDTS compatibility gate), `8161db2f73`
(signed manifests and URI policy). No deployment or push was performed. No dependency source was modified.

## Issue 1

Root cause: `ActivateBestChain` treated a matching non-null `pblock` as a
prerequisite for deep-reorg parking. Missing-ancestor-last delivery and
`ActivateBestChain(nullptr)` can choose a descendant without that condition.

The subsequent acceptance-time parking task supersedes the initial
activation-time `DeepReorgPolicy` fix and its parking test descriptions.
Automatic parking is now determined in `AcceptBlock` before candidate
propagation, and unpark uses ordinary null-body activation. Current architecture,
regressions, import/rebuild behavior and exact results are recorded in
[deep-reorg-parking-review.md](deep-reorg-parking-review.md). The RDTS and
manifest implementation and original validation below are unchanged.

## Issue 2

Root cause: permanent RDTS used BIP9 `StateSinceHeight` as its input-creation
exemption boundary. That boundary moved when the underlying deployment changed
state even though Purity's permanent activation had already occurred.

Intended consensus rule: permanent RDTS starts at `nPurityActivationHeight`;
mainnet's fixed UTXO creation boundary is 961637. Creation heights below the
boundary retain the spend-side exemption; equality and later heights do not.
Later BIP9 transitions cannot move the corrected boundary.

Fix: explicit `nReducedDataGrandfatherHeight`, independent
`nReducedDataGrandfatherFixHeight`, and `GetReducedDataGrandfatherHeight` used by
`ConnectBlock`. Generic BIP9 and inactive/expired behavior remain unchanged.
Regtest activation options expose both heights for testing, and retain its
fixed difficulty when low-height Purity activation is selected.

Current mainnet parameters (2026-10-06):
`nReducedDataGrandfatherFixHeight = nPurityActivationHeight = 961637`. The
creation boundary is also 961637, so mainnet blocks from Purity activation use
the fixed boundary. Earlier blocks retain historical behavior. This supersedes
the initial unset (`INT_MAX`) correction gate in `edf5362ea9`.

A candidate scan through active-chain tip 967297 was reported with no
obvious dependent confirmed transaction. No tip hash/chainwork or complete
corrected-rule mainnet revalidation was supplied. This code decision is not a
claim of historical compatibility or deployment. The RPC candidate scan and
isolated full revalidation procedure are in
[purity-consensus.md](purity-consensus.md#historical-audit-procedure).

Unit tests: `chainparams_tests/reduced_data_grandfather_height` covers real BIP9
DEFINED/STARTED/LOCKED_IN/ACTIVE, before/at/after activation, equality,
unset/forward-only correction gates, generic BIP9 and expiry.
`reduced_data_mainnet_correction_at_purity_activation` replaces the original
`reduced_data_mainnet_correction_unresolved` test and checks the selected mainnet
parameter/helper at 961637, 961638, 965664, 967297 and 970000.
`pow_tests/purity_regtest_retains_no_retargeting` remains unchanged.

Functional tests: added and registered `feature_purity_rdts_grandfather.py`.
UTXOs are created at 199, 200 and 201 with Purity/correction activation at 200.
Valid Taproot script-path witnesses execute Tapscript OP_IF, which RDTS
prohibits. After BIP9 STARTED, LOCKED_IN and ACTIVE, pre-Purity outputs remain
spendable and equality/post-Purity outputs fail block validation with the
specific OP_IF/NOTIF script error. Block validation is used because mempool
policy would independently reject these nonstandard spends.

Old behavior reproduced with only the regtest test configuration support:
the underlying deployment entered LOCKED_IN at 432 and the old boundary
accepted the equality UTXO's prohibited spend in block 433. Corrected regtest
validation rejects that spend throughout all tested transitions. This is not a
mainnet history audit.

## Issue 4

Root cause: UniValue preserves duplicate keys; canonicalization collapsed them
with last-value semantics while consumers read the first value.

Exploit condition: replacing a remote manifest response permits an unsigned
malicious first value alongside the legitimate signed last value. Possession
of the signing private key is unnecessary.

Fix: shared `JsonHasDuplicateKeys` traverses the original public UniValue keys
and values recursively, including objects inside arrays. All three parsers
reject duplicates before semantic access; the shared signature/digest path
rejects before canonicalization or signature extraction. The canonicalizer is
internal and only receives a checked DOM. Identical and decoded-equivalent
keys are rejected. Local manifest fixtures also use strict DOMs; general
JSON-RPC parsing and UniValue dependency sources are unchanged.

URI hardening: HTTPS artifacts on github.com require the exact official
`/saltduck/bitcoinpurity/releases/download/` namespace. Userinfo, ports,
misleading hosts, dot segments, backslashes and escaped paths are rejected.
`downloads.bitcoinpurity.org` and `release-assets.githubusercontent.com` remain
allowed; objects.githubusercontent.com is rejected without evidence of a
required official redirect. Encoded signed CDN query parameters remain valid.
Qt uses `UserVerifiedRedirectPolicy`, checks each resolved destination before
`redirectAllowed`, and checks the final URL before accepting the download.

A live HEAD check of the official
`Bitcoin-Purity-1.0.0-arm64-apple-darwin.zip` release artifact followed a redirect
to release-assets.githubusercontent.com and returned 200. This checks the
current route for that artifact, not the GUI download/install flow. Official
package URI restrictions are unchanged. Size/SHA256 verification and manual
installation are retained; SHA256 authenticity derives from the signed manifest.

Unit/integration tests:

- `software_updates_tests/reject_duplicate_manifest_keys` covers schema,
  latest, signature, version, urgency, release_notes_url, artifacts, platform,
  download_uri, archive_sha256, archive_size_bytes and other existing fields.
- `duplicate_first_value_signature_exploit` inserts an attacker-controlled
  FIRST latest object and retains the legitimately signed LAST object. It
  demonstrates first/last disagreement and that collapsing to the last object
  gives the legitimate digest, then checks rejection by verification and the
  real parser. Duplicate JSON is constructed by raw string insertion.
- `reject_unofficial_release_uris` exercises official namespace/CDN acceptance,
  arbitrary repositories, HTTP, userinfo, host/path variants, ports and traversal.
- `official_packages_tests/reject_duplicate_manifest_keys` covers packages and
  package fields, nested object-array-object-object recursion and escaped keys.
  Extended `parse_published_mainnet_manifest` injects duplicates into an actual
  embedded-key signed fixture and feeds the REMOTE_SIGNED parser.
- `official_broadcasts_tests/reject_duplicate_manifest_keys` covers notices,
  id, body, expires_at and other notice fields.

These fixtures call the actual kernel parsers and shared verifier; ambiguous
releases/packages/notices do not reach consumers. There is no dedicated GUI
HTTP fixture in the existing framework, so no artificial network framework was
added. The downloader was compiled in the full GUI build; the existing Qt suite
passed. End-to-end GUI redirect/error/file-download behavior was not exercised.
Restoring the old duplicate acceptance/canonicalization semantics produced
38 software, 17 package and 16 broadcast failed assertions (71 total).

## Test results

The following commands/results apply to the original TASK-014 sources. Current
acceptance-time parking verification is in
[deep-reorg-parking-review.md](deep-reorg-parking-review.md).

```sh
cmake -S . -B build -G Ninja -DBUILD_GUI=ON -DBUILD_DATUM=OFF -DHAVE_DECL_PIPE2=0 -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5
cmake --build build -j 8
build/bin/test_bitcoin --run_test=software_updates_tests,official_packages_tests,official_broadcasts_tests,chainparams_tests,validation_chainstate_tests,validation_chainstatemanager_tests,pow_tests,versionbits_tests --log_level=message
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -j 4
python3 build/test/functional/test_runner.py --jobs=3 feature_park_deep_reorg.py feature_purity_rdts_grandfather.py feature_rdts.py feature_bip9_max_activation_height.py feature_reduced_data_utxo_height.py feature_reduced_data_temporary_deployment.py rpc_getchaintips.py --tmpdirprefix=/tmp/purity-review-complete
git diff --check
```

- Full build: daemon, CLI, GUI, unit, Qt and dependency test programs built.
- Focused C++ suites: 85 test cases, no errors.
- Full CTest: 148/148 passed, including Qt.
- Functional tests: 7/7 passed, including both new regression scenarios and
  existing RDTS/BIP9 compatibility tests.
- `git diff --check`: passed.

The macOS iconutil stage failed inside the sandbox but succeeded when the same
build command could access macOS system services. No icon or unrelated source
repair was made. The macOS build uses `HAVE_DECL_PIPE2=0` and Qt offscreen tests.
DATUM is disabled in this build; DATUM behavior is outside this task.

Old-semantics regression commands (expected nonzero results) included:

```sh
build/bin/test_bitcoin --run_test=validation_chainstate_tests/deep_reorg_null_block_and_unpark --log_level=message
build/bin/test_bitcoin --run_test=software_updates_tests/reject_duplicate_manifest_keys,duplicate_first_value_signature_exploit --log_level=message
build/bin/test_bitcoin --run_test=official_packages_tests/reject_duplicate_manifest_keys --log_level=message
build/bin/test_bitcoin --run_test=official_broadcasts_tests/reject_duplicate_manifest_keys --log_level=message
python3 test/functional/feature_park_deep_reorg.py --configfile=build/test/config.ini --tmpdir=/tmp/purity-baseline-parking-3
python3 test/functional/feature_purity_rdts_grandfather.py --configfile=build/test/config.ini --tmpdir=/tmp/purity-baseline-rdts-3
```

The parking predicate/duplicate semantics were temporarily restored only for
negative-control unit runs with corrected fixtures, then restored before final
builds and passing tests. RDTS functional baseline retained the original
ConnectBlock boundary selection while adding regtest-only test infrastructure.

## Remaining concerns

Mainnet correction activation is now selected at 961637 in the current source
(see TASK-017). Complete historical compatibility validation and production
rollout have not been demonstrated here. Nodes using the old moving boundary
can disagree with corrected nodes if affected spends are present.

Publishers must remove duplicate keys and use the supported artifact URI and
redirect policy. Previously accepted ambiguous manifests or unsupported URI
hosts/paths are deliberately rejected. No production rollout, CI run,
independent artifact-signing system or automatic installation is claimed.
