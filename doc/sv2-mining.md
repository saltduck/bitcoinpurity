# Connect miners to a Stratum V2 pool through Purity Node

Purity Node includes a gateway for existing Stratum V1 miners:

```text
Stratum V1 miner → Purity Node embedded gateway → Stratum V2 pool
```

The node selects transactions locally and declares its template to the pool.
It sends work to miners after the pool confirms the custom mining job.
You do not need to run a separate gateway or expose a Template Provider port.

## Requirements

- Run a Purity Node build with `BUILD_SV2=ON` (the default build setting).
  The gateway is disabled at runtime until you set `sv2=1`.
- Use mainnet or regtest. Other networks, including Signet, are not supported
  by this gateway.
- Synchronize the node with the network before mining.
- Obtain the following from the pool operator:
  - Mining endpoint (`host:port`).
  - Full-Template Job Declaration Server (JDS) endpoint (`host:port`).
  - Authority public key for each endpoint.
  - Upstream account identity for the gateway.
- The pool must support the Full-Template Job Declaration protocol dialect
  used by this implementation. A Mining endpoint alone is insufficient.

The complete chain has been tested with an independent local pool on macOS
and Linux. Production pool interoperability requires separate acceptance;
this does not establish compatibility with every Stratum V2 pool. Native
Windows MSVC/Qt acceptance remains outstanding.

## 1. Configure Purity Node

Edit the `bitcoin.conf` used by your node. See the
[configuration file guide](bitcoin-conf.md) for locating it or selecting an
explicit file with `-conf`.

The example below assumes that the node's LAN address is `192.168.1.10`.
Replace that address, both example pool endpoints, both public-key placeholders,
the account identity and the local password with your actual values.

```ini
# Enable RPC for local CLI status checks, including when running the GUI.
server=1

# DATUM Solo and SV2 cannot run at the same time.
datum=0
sv2=1

# Stratum V1 listener for your miners.
sv2listen=192.168.1.10
sv2port=23334

# Upstream endpoints: host:port, without a URL scheme.
sv2pool=mining.example.com:3336
sv2jds=jds.example.com:3337
sv2poolkey=<MINING_AUTHORITY_PUBLIC_KEY>
sv2jdskey=<JDS_AUTHORITY_PUBLIC_KEY>
sv2identity=<POOL_ACCOUNT_IDENTITY>

# Local miner credentials, separate from the upstream pool account.
sv2auth=1
sv2user=farm
sv2password=<LOCAL_MINER_PASSWORD>

# Connection limits.
sv2maxclients=32
sv2maxperip=4
```

Authority keys accept either 32-byte hexadecimal or SRI Base58 encoding. Use
the public keys supplied by the pool operator; these fields do not take private
keys. Mining and JDS may use different authority keys.

The default listener is `127.0.0.1:23334`, which is accessible only from the
node's own computer. For miners on your LAN, use the node's LAN address as
shown above and allow inbound TCP port `23334` from the miner network.
`sv2listen` takes a numeric IP address, without a port.

The default limits are 32 connections in total and four per source IP. If
several miners share one source IP, adjust `sv2maxperip` accordingly.

Keep the local password in the configuration file. Avoid `#`, newlines and
leading or trailing whitespace in values entered through Qt settings.
Passwords are excluded from `settings.json`, status snapshots and logs.

Restart the node after changing the configuration. Command-line arguments
retain their normal precedence over configuration-file values.

## 2. Configure each miner

In the miner's pool settings, enter:

| Miner setting | Example |
| --- | --- |
| Pool URL | `stratum+tcp://192.168.1.10:23334` |
| Worker / username | `farm.worker01` |
| Password | The value of `sv2password` |

Use `farm.worker02`, `farm.worker03`, and so on for other miners. With
`sv2user=farm`, the gateway accepts `farm` or `farm.` followed by a nonempty
worker name. When `sv2auth=1`, the password must match `sv2password`.

The miner connects to Purity Node using Stratum V1. The node connects to the
pool using Stratum V2. The miner's local worker name is not the upstream
account: `sv2identity` identifies the gateway to the pool, and worker names
remain local.

Local authentication is optional (`sv2auth=0` by default). The example enables
it explicitly.

## 3. Check operation

Run the CLI against the same node and configuration:

```sh
bitcoin-cli getsv2info
```

If the node uses an explicit configuration file or data directory, pass the
same `-conf` or `-datadir` option to the CLI. On Windows, use `bitcoin-cli.exe`.

| Field | Meaning |
| --- | --- |
| `enabled` | SV2 is enabled in the active node configuration. |
| `running` | The gateway has usable, pool-confirmed work. |
| `status` | Current lifecycle state, such as `connecting`, `declaring`, `running` or `retrying`. |
| `clients` | Connected miner sessions. |
| `authorized_clients` | Miner sessions that passed local authentication. |
| `current_height` | Height of the current local template. |
| `local_valid_shares` | Shares that passed local checks. |
| `accepted_shares` | Shares acknowledged as accepted by the upstream pool. |
| `rejected_shares` | Rejected shares. |
| `pending_shares` | Submitted shares awaiting an upstream response. |
| `abandoned_shares` | Pending submissions whose connection was lost before acknowledgement. |
| `blocks_accepted` | Candidate blocks accepted through local node validation. |
| `last_block_result` | Most recent local block submission result. |
| `error` | Sanitized connection or job error. |

Expect `status="running"` and `running=true` before mining. After a miner
connects, `authorized_clients` should increase. A locally valid share does not
count as pool-accepted until the pool acknowledges it.

The disabled snapshot contains only `enabled`, `running` and `status`. Other
fields are present when the gateway is enabled. The Qt Mining page reads the
same status snapshot as this RPC.

## Troubleshooting

| Symptom | Checks |
| --- | --- |
| Miner cannot connect | Check the node's LAN address, listener port and firewall. A listener on `127.0.0.1` is unavailable to LAN miners. Check total and per-IP connection limits. |
| Miner authorization fails | Check `sv2auth`, the `sv2user` / `user.worker` format and `sv2password`. These are local credentials. |
| Status stays at `connecting` or `retrying` | Check both pool endpoints, reachability, authority public keys, upstream identity and `error`. |
| Status stays at `declaring` | Check that the pool's Mining/JDS services support this Full-Template protocol dialect and accept locally declared jobs. |
| Status is `waiting-for-local-template` | Check node synchronization and local template availability. |
| Shares are locally valid but not accepted | Compare `pending_shares`, `rejected_shares`, `abandoned_shares` and `error`. Only upstream acknowledgement increases `accepted_shares`. |
| Node rejects the configuration | Ensure `datum=1` and `sv2=1` are not both set, use mainnet or regtest, and check required endpoints, keys and identity. |
| SV2 options or RPC are unavailable | Confirm that the executable was built with `BUILD_SV2=ON`. |

On pool disconnect, authentication failure or job rejection, the gateway stops
providing work and closes miner connections. It reconnects to the configured
pool with bounded backoff and declares fresh work. It does not replay old
shares, switch to another pool or fall back to Solo mining. The node continues
its normal non-mining operation while the pool is unavailable.

## Further details

- [Configuration and RPC contract](../docs/api/sv2.md)
- [Embedded gateway architecture](../docs/architecture/embedded-sv2.md)
- [Build, test and release acceptance status](../docs/tasks/TASK-035.md)

## Optional fixed coinbase tags

`sv2pooltag` and `sv2minertag` default to empty and are also available in Qt.
Obtain any required pool tag from the operator; the protocol does not announce
it and Purity does not infer it from an endpoint or account. Both tags together
use at most 59 UTF-8 bytes including one push byte for each nonempty tag. Control
characters are rejected. The actual scriptSig is checked against 100 bytes.
Tags precede the negotiated extranonce data push and cannot be changed by miners.
Changes take effect after restart, with existing command-line/config precedence.
The payout remains the local template's available reward, preserving required
outputs and witness. There is no named-pool mode or fixed PUSH12 requirement.

When upstream work is unavailable, a new mining.subscribe receives error 20
("Upstream work unavailable") followed by connection close. No extranonce is
allocated. Configure/local authorization can respond before subscription but do
not imply upstream readiness. Existing subscribed miners disconnect when work is
withdrawn. Limits and the ten-second subscription/authorization deadline remain.
