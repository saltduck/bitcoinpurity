# Purity Signet configuration and RPC contract

`-signet` / `-chain=signet` selects the one Purity Signet chain. RPC `chain`
remains `signet`; the config section remains `[signet]`. No Purity mode option.

The owner authorized `-signetparams=<JSON file>` and an explicitly labelled
simulation example. Schema version 1 and reader validation tests are in place. JSON contains
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

## Parameter schema (version 1)

Use `-signetparams=/absolute/path/to/parameters.json`, or place
`signet-params.json` in the base data directory. Relative paths resolve against
that base directory. Missing configuration prevents Signet startup. Mainnet
does not read the file, including when building argument help.

| Field | Contract |
| --- | --- |
| schema_version | Integer 1 |
| simulation | Boolean; true marks local simulation and permits an initially null activation_hash |
| activation_height | Integer in [6, INT_MAX) |
| parent_hash | Nonzero 32-byte hex hash at H-1 |
| activation_hash | Nonzero H hash; initially null only in a labelled simulation |
| anchor | Object containing height, hash, bits, parent_hash, parent_time |
| anchor.height | At least 1 and less than H |
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
simulation, activation_height, parent_hash, activation_hash,
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

## Owner-fixed launch height and defaults (2026-10-15)

The launch example [signet-params.json](../../contrib/purity-signet/signet-params.json)
uses H=318621 and anchor H-5=318616. Default P2P/RPC ports are 38333/38332;
state uses signet/ under the base datadir. simulation.json remains the H=11
fast fixture. H-1/anchor header hashes, links and PoW were checked from public
raw headers; this is not a full 318620-block/UTXO validation or independent
operator attestation. The launch example still uses fixture public keys,
nonproduction seeds, a null first-block pin and simulation=true. Its minimum
chainwork retains the older original Signet baseline threshold, not an asserted
H-1 cumulative-work value. Freeze all remaining official values before release.

The six real boundary blocks were additionally checked for merkle, witness and
original BIP325 signatures. Details: [source audit](../purity-signet-review.md).
