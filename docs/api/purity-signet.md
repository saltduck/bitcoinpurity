# Purity Signet configuration and RPC contract

`-signet` / `-chain=signet` selects the one Purity Signet chain. RPC `chain`
remains `signet`; the config section remains `[signet]`. No Purity mode option.

The owner authorized `-signetparams=<JSON file>` and an explicitly labelled
simulation example. Schema versions 1 (fixed) and 2 (preparing or fixed) are supported. JSON contains
public keys only. Fixed original challenge, genesis,
magic, powLimit, 600s spacing and 86400s half-life cannot be overridden.
Mainnet never consumes this file. Legacy challenge/blocktime overrides cannot
select a different consensus chain. Seeds remain connection configuration.

GBT accepts the signet/segwit rules and returns the challenge for its own
height and the actual required bits. Proposal verification checks signatures
even without PoW. Chain/mining RPCs disclose challenge height and audited fork
parameters. Public RPCs cannot choose unsigned-template validation.

Use separate configs, ports, cookies, wallets and directories for external
Bitcoin Core Signet and this client. Changing chain parameters is not a way
to migrate an existing incompatible database. Missing/configuration errors
are reported without printing credentials.

## Parameter schema (versions 1 and 2)

Use `-signetparams=/absolute/path/to/parameters.json`, or place
`signet-params.json` in the base data directory. Relative paths resolve against
that base directory. Missing configuration prevents Signet startup. Mainnet
does not read the file, including when building argument help.

| Field | Contract |
| --- | --- |
| schema_version | Integer 1 or 2 |
| simulation | Boolean; fixed files require true while activation_hash is null |
| activation_height | Integer in [6, INT_MAX), or null in v2 preparation |
| parent_hash | Nonzero 32-byte hex hash at H-1, or null in v2 preparation |
| activation_hash | Null in preparation; fixed files require a nonzero H hash unless simulation=true |
| anchor | Object containing height, hash, bits, parent_hash, parent_time, or null in v2 preparation |
| anchor.height | H-5 in v2; at least 1 and less than H in v1 |
| anchor.bits | Four-byte big-endian compact hex, positive nonoverflow target at/below original powLimit |
| anchor.parent_time | Nonzero uint32 timestamp |
| public_keys | Exactly three distinct valid compressed keys, in challenge order |
| minimum_chain_work | Nonzero 32-byte hex chainwork, audit against H-1 |
| p2p_port / rpc_port | Valid distinct TCP ports |
| dns_seeds | Array of seed names |
| fixed_seed_nodes | Array of numeric address:port endpoints, serialized as BIP155 seeds |
| historical_seed_nodes | Array of explicit old-history endpoints, typically port 38333 |

Duplicate keys and unknown top-level fields are rejected. Values are loaded
for the selected Signet process; the RPC client uses the same configured RPC
port. Legacy challenge/blocktime overrides are rejected.

The [simulation example](../../contrib/purity-signet/simulation.json) uses the
real first ten Signet blocks as inherited history, H=11 and anchor=6. Its new
keys are public test-fixture keys, its DNS name ends in .invalid and its fixed
seed is loopback. It is not an official network configuration. No private keys
are stored in the JSON. Never publish this configuration as official.

## Implemented Signet RPC fields

GBT's signet_challenge belongs to its own height. Blockchain/mining info return
signet_challenge_height=blocks+1 and the corresponding next-block challenge.
These fields are Signet-only. Blockchain info also returns signet_fork:
state (preparing or fixed), simulation, activation_height, parent_hash, activation_hash,
anchor_height/hash/bits/parent_hash/parent_time, historical_challenge,
postfork_challenge, message_start, pow_limit, minimum_chain_work,
target_spacing and half_life. An unfrozen simulation activation_hash is
the all-zero RPC hash; JSON configuration represents it as null.

getpeerinfo.signet_chain_verified records whether a peer supplied a fully
validated H-or-later block. It does not derive membership from services or
user-agent text. Expected fork announcements do not add peers to the ban list.

The standalone miner-simulation.json selects node/signer CLI argument arrays,
chain_params, bitcoin_util, reward_script and poll_seconds (1..60). Signer
arrays must be distinct and exactly two. Chain parameters contain public keys
only. CLI secrets belong in private node configuration/cookies.

Operational freeze and recovery: [operator procedure](../purity-signet-operations.md).

## Preparation and launch freeze

The launch example signet-params.json is now schema v2 preparation; H=318621 is
an obsolete rehearsal record. All activation_height, parent_hash, anchor and
activation_hash must be explicitly null together. Public keys, ports, seeds,
minimum work and all required field checks still apply. A fixed v2 anchor must
be H-5. Version 1 fixed files retain their original anchor validation.

getblockchaininfo.signet_fork.state and getmininginfo.signet_state report
preparing or fixed. In preparation, all activation/anchor RPC fields are null;
historical and post-fork challenges remain visible. Mining generation RPCs,
GBT templates and proposals fail with RPC -1, including before H. Historical
submitblock/submitheader remain available for synchronization/import. Internal
block assembly is blocked too. After freezing, the existing fixed RPC fields
and first-pin zero-hash convention remain unchanged.

prepare.py --config PREPARING --output FIXED --node-cli JSON_ARRAY --ready
requires explicit operational readiness, synced unpruned history and the same
signing keys. It pauses networking, runs verifychain 4 0, derives one snapshot,
rejects stale parents (default --max-parent-age 3600), writes a companion
.snapshot.json record, requests normal stop, then creates the fixed file without
overwriting existing outputs. Failed checks after pausing leave networking
paused. The operator must wait for clean exit and back up before restarting;
keep other import/RPC writers stopped during freezing. It emits simulation=true
and activation_hash=null for the first-block workflow. Readiness attests wallet
custody and actual endpoints; the tool cannot prove remote signing custody.

Changing a fixed consensus fingerprint on an existing index fails startup.
Adding the first-block hash and changing simulation are allowed; active pins
remain checked. Conflicting inactive branches are excluded. A legacy index
without a fingerprint and active H+ state requires -reindex-chainstate; changing
H/keys/anchor requires restoration or -reindex. Defaults remain signet/ and
38333/38332. No Mainnet JSON or database migration is introduced.
