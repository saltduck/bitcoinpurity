# Purity Signet deployment: choose H immediately before mining

This is the English edition of the operator's Chinese deployment instructions.
The Chinese original remains outside the repository. The launch sequence is:
prepare and synchronize -> freeze the validated tip T -> generate H=T+1
parameters -> restart -> mine H -> pin the first block hash.

H=318621 is an obsolete rehearsal height, not a launch setting. Every
`/path/to/...` or `/absolute/path/to/...` below is a placeholder that must be
replaced before use. Run commands from the Bitcoin Purity repository root
unless stated otherwise. These instructions do not start a network or select H
automatically. For an already launched chain, use its official parameters;
do not repeat preparation to choose another H or authority.

The commands follow the current [configuration and RPC contract](api/purity-signet.md)
and [operator procedure](purity-signet-operations.md). In particular, preparation
uses the bounded-memory verification strategy implemented in `prepare.py`,
rather than the full-history level-4 check in the older Chinese source.

## 1. Build and prepare an independent data directory

```sh
cmake -S . -B build -DBUILD_GUI=OFF -DBUILD_DATUM=OFF
cmake --build build -j4
mkdir -p /path/to/purity-signet
cp contrib/purity-signet/bitcoin.conf /path/to/purity-signet/bitcoin.conf
cp contrib/purity-signet/signet-params.json /path/to/purity-signet/signet-params.json
```

Edit the copied `signet-params.json`:

- Use `schema_version: 2`. Keep `activation_height`, `parent_hash`, `anchor`
  and `activation_hash` all explicitly `null`; partial activation settings
  are not permitted.
- Replace `public_keys` with the three actual signers' compressed public keys.
  Agree on and retain their order. The sample keys are public test authorities
  and cannot secure an official network.
- Replace Purity seeds with operated endpoints. `.invalid` names and loopback
  addresses are simulation values. `historical_seed_nodes` supplies original
  history during preparation.
- Keep `simulation: true` during preparation. Validate the remaining fields
  against the schema and check the minimum-chain-work setting.

The client continues to use `chain=signet` and `[signet]`, with default P2P/RPC
ports 38333/38332. Chain data resides in `signet/` under the independent base
data directory. When running original Core on the same host, use different
base data directories and override conflicting listener ports.

Keep `assumevalid=0`, disable pruning and do not use an AssumeUTXO snapshot.
Do not copy another node's indexes or chainstate as a substitute for validation.

### Generate signing public keys before starting Purity (optional)

If the Purity node has not started, use a Bitcoin Core binary with wallet RPC
support to start a temporary regtest wallet node. Use a new independent
directory, never an existing mainnet or Purity directory. This example is for
signer A; B and C use their own directories and wallet names.

```sh
mkdir -p /path/to/signer-a
build/bin/bitcoind -regtest -datadir=/path/to/signer-a
```

Keep that node running. In another terminal, check wallet RPC support, create
the wallet and generate an address:

```sh
build/bin/bitcoin-cli -regtest -datadir=/path/to/signer-a help createwallet
build/bin/bitcoin-cli -regtest -datadir=/path/to/signer-a \
  -named createwallet wallet_name=authority-a descriptors=true load_on_startup=true
build/bin/bitcoin-cli -regtest -datadir=/path/to/signer-a \
  -rpcwallet=authority-a getnewaddress "purity-signet-authority" "bech32"
```

Pass the returned address to `getaddressinfo` and save its `pubkey`:

```sh
build/bin/bitcoin-cli -regtest -datadir=/path/to/signer-a \
  -rpcwallet=authority-a getaddressinfo "REPLACE_WITH_RETURNED_ADDRESS"
```

Use the 66-character compressed public key beginning with `02` or `03`.
Do not put an address or xpub in `public_keys`. The regtest address is not a
Purity Signet payout address and does not need funding. A, B and C each
generate one key and enter the three public keys in the agreed script order.
Save and reuse those addresses; calling `getnewaddress` again generates a
different key and does not recover the configured authority.

The default regtest ports support one temporary node at a time. If another
regtest node is running on the host, stop it before setting up the next signer
or assign different ports. If `help createwallet` reports an unknown method,
the daemon does not expose wallet RPC, for example because wallet support was
not built or was disabled. Use a wallet-enabled binary without modifying a
running mainnet node.

If these wallets will be mining signers, retain their wallet directories.
All three use the same consensus script order, but each private-key-enabled
wallet must import its own private signing descriptor: its own private derived
key expression and the other two public keys. Do not import a public-only bare
`multi(2,keyA,keyB,keyC)` into a private-key-enabled wallet. The local import
procedure below supplies the required private expression.

The miner uses Bitcoin Core wallet RPC; a Sparrow wallet cannot directly act
as that RPC endpoint. Protect wallets, recovery material and backups. Never put
private keys in chain parameters or miner configuration.

Public-key creation can use the default ports sequentially. Concurrent
preflight or signing requires three independent daemons and distinct RPC ports
on the same host. These daemons provide wallet signatures and do not need to
synchronize Purity blocks. Use `-listen=0` when their P2P listeners are unnecessary.

### Import each wallet's local private signing descriptor before preflight

A descriptor wallet needs an explicit private signing descriptor. The script
below extracts the derived key expression for the selected address from the
local wallet's private external `wpkh` descriptor. It retains the other two
public keys, verifies that the address matches the configured signer, and
imports the descriptor directly into that wallet. Private descriptors are used
only in local process memory and wallet RPC; they are not printed or saved.

Copy the entire shell block, from `python3 - <<'PY'` through the final `PY`,
into zsh. Do not paste only its Python body. Run it once for each signer with
`signer_index` 0, 1 or 2 and that wallet's saved address, data directory and RPC
port. First check that the address's public key occupies the matching position
in the parameter file and unlock an encrypted wallet through a protected
interface. If its `bitcoin.conf` already sets `chain=regtest`, remove `-regtest`
from the CLI array: select the network in exactly one way.

```sh
python3 - <<'PY'
import json
import subprocess

bitcoin_cli = '/absolute/path/to/build/bin/bitcoin-cli'
params_path = '/path/to/purity-signet/signet-params.json'
signer_index = 0  # A=0, B=1, C=2
signer_address = 'REPLACE_WITH_THIS_WALLETS_SAVED_ADDRESS'
cli = [
    bitcoin_cli,
    '-regtest',
    '-rpcport=19443',
    '-datadir=/path/to/signer-a',
    '-rpcwallet=authority-a',
]


def rpc(method, *args):
    lines = [arg if isinstance(arg, str) else json.dumps(arg, separators=(',', ':')) for arg in args]
    result = subprocess.run(cli + ['-stdin', method], input='\n'.join(lines) + '\n',
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f'{method} failed (exit {result.returncode}); inspect locally without sharing private RPC output')
    return json.loads(result.stdout)


params = json.loads(open(params_path).read())
public_keys = params['public_keys']
if len(public_keys) != 3 or signer_index not in (0, 1, 2):
    raise SystemExit('The parameters must contain three public keys; signer_index must be 0, 1 or 2')

address_info = rpc('getaddressinfo', signer_address)
if address_info.get('pubkey', '').lower() != public_keys[signer_index].lower():
    raise SystemExit('The wallet address public key does not match the configured signer')
address_index = address_info['hdkeypath'].split('/')[-1]

entries = rpc('listdescriptors', True)['descriptors']
candidates = [
    item for item in entries
    if item['desc'].startswith('wpkh(')
    and item.get('active')
    and not item.get('internal')
    and item['desc'].split('#', 1)[0].endswith('/*)')
]
if len(candidates) != 1:
    raise SystemExit('No unique active external private wpkh descriptor; retry in the newly created signing wallet')

derived = rpc('deriveaddresses', candidates[0]['desc'], [int(address_index), int(address_index)])
if derived != [signer_address]:
    raise SystemExit('The local private descriptor did not reproduce the address; import stopped')

key_expression = candidates[0]['desc'].split('#', 1)[0][5:-1]
key_expression = key_expression[:-1] + address_index
if 'prv' not in key_expression.lower():
    raise SystemExit('The wallet returned no private key expression; do not substitute a public descriptor')

signer_keys = list(public_keys)
signer_keys[signer_index] = key_expression
unsigned_descriptor = 'multi(2,' + ','.join(signer_keys) + ')'
descriptor_info = rpc('getdescriptorinfo', unsigned_descriptor)
if not descriptor_info.get('hasprivatekeys'):
    raise SystemExit('The signing descriptor contains no local private key')

imported = rpc('importdescriptors', [{
    'desc': unsigned_descriptor + '#' + descriptor_info['checksum'],
    'timestamp': 'now',
}])
if len(imported) != 1 or not imported[0].get('success'):
    raise SystemExit('Import failed; do not share wallet RPC output or private descriptors')
print('Local private signing descriptor imported.')
PY
```

A uses RPC port 19443 and index 0, B uses 19444/1, and C uses 19445/2. Each
signer runs this on its own wallet machine. Never copy a private descriptor to
another signer or the miner host. For distributed signers, copy only the public
parameter file, not wallet directories. Each imported signing descriptor must
contain exactly that signer's private key and the other two public keys.

## 2. Synchronize history while preparing the signers

Start the Purity node in one terminal:

```sh
build/bin/bitcoind -datadir=/path/to/purity-signet
```

Check it in another:

```sh
build/bin/bitcoin-cli -datadir=/path/to/purity-signet getblockchaininfo
build/bin/bitcoin-cli -datadir=/path/to/purity-signet getmininginfo
```

Wait for `initialblockdownload=false` and `headers=blocks`. Confirm
`signet_fork.state=preparing` and null activation parameters. The preparing
node validates original Signet blocks, transactions, signatures and difficulty
itself. Original Core reference nodes are optional cross-checks, not a required
dependency. Mining RPCs and templates are unavailable while preparing.

During synchronization, finish signer setup, encrypted backups and recovery
rehearsals. Each wallet imports its own private signing descriptor; all three
must expand to the same ordered bare 2-of-3 consensus script. Actual mining
selects two distinct wallets. Keep keys and passphrases out of public parameters
and miner configuration. Prefer local cookie authentication; remote signing RPC
needs separately protected transport and access controls.

### Pre-freeze checks: signers and Purity P2P endpoints

Replace the sample keys with the three actual compressed keys and have all
signers confirm their order. Each wallet must already have its local private
signing descriptor, containing only its own private key and the other two
public keys. Run the import procedure on the corresponding wallet host before
this preflight. Unlock encrypted wallets through a protected interface; do not
put passphrases in commands, scripts or configuration files.

#### A. Start all three signing daemons and check their wallets

The preflight calls A, B and C, so all three daemons must be running with their
wallets loaded. These are wallet signing endpoints, not three Purity consensus
nodes; they need wallet RPC access, not a synchronized Purity chain.

For three signers on the same host, start each daemon with its own data
directory and RPC port. Disable unnecessary P2P listening to avoid conflicts:

```sh
build/bin/bitcoind -regtest -listen=0 -daemon -datadir=/path/to/signer-a -rpcport=19443
build/bin/bitcoind -regtest -listen=0 -daemon -datadir=/path/to/signer-b -rpcport=19444
build/bin/bitcoind -regtest -listen=0 -daemon -datadir=/path/to/signer-c -rpcport=19445
```

If a signer from section 1 is still running on a different RPC port, stop it
cleanly before restarting with these settings. Remove `-regtest` from commands
whose data directory already selects `chain=regtest` in `bitcoin.conf`.

Check `listwallets`. Run the corresponding `loadwallet` only if the wallet is
not already loaded, then inspect `getwalletinfo`:

```sh
build/bin/bitcoin-cli -regtest -datadir=/path/to/signer-a -rpcport=19443 listwallets
build/bin/bitcoin-cli -regtest -datadir=/path/to/signer-a -rpcport=19443 loadwallet authority-a
build/bin/bitcoin-cli -regtest -datadir=/path/to/signer-a -rpcport=19443 -rpcwallet=authority-a getwalletinfo

build/bin/bitcoin-cli -regtest -datadir=/path/to/signer-b -rpcport=19444 listwallets
build/bin/bitcoin-cli -regtest -datadir=/path/to/signer-b -rpcport=19444 loadwallet authority-b
build/bin/bitcoin-cli -regtest -datadir=/path/to/signer-b -rpcport=19444 -rpcwallet=authority-b getwalletinfo

build/bin/bitcoin-cli -regtest -datadir=/path/to/signer-c -rpcport=19445 listwallets
build/bin/bitcoin-cli -regtest -datadir=/path/to/signer-c -rpcport=19445 loadwallet authority-c
build/bin/bitcoin-cli -regtest -datadir=/path/to/signer-c -rpcport=19445 -rpcwallet=authority-c getwalletinfo
```

Calling `loadwallet` for an already loaded wallet returns an error; skip that
command in this case. Both `descriptors` and `private_keys_enabled` must be
true. For an encrypted wallet, `unlocked_until` must be in the future. Remote
deployments need the matching `-rpcconnect` and authentication settings. Do not
expose wallet RPC to untrusted networks.

Test all three pairs: A+B, A+C and B+C. The script below reads the public keys,
uses the production miner's BIP325 PSBT construction, passes each PSBT through
two wallets and checks `finalizepsbt`. The PSBT and header are temporary test
objects. It does not broadcast transactions, submit blocks or mine. Preparation
correctly refuses GBT, so the test does not require a fixed H.

Run this complete block from the repository root:

```sh
python3 - <<'PY'
import itertools
import json
from pathlib import Path
import runpy
import subprocess

params_path = Path('/path/to/purity-signet/signet-params.json')
bitcoin_cli = '/absolute/path/to/build/bin/bitcoin-cli'

# Match each daemon's network and endpoint. These examples have no chain setting in bitcoin.conf.
# If a data directory already selects a chain, remove its explicit -regtest or -signet flag.
signer_clis = [
    [bitcoin_cli, '-regtest', '-rpcport=19443', '-datadir=/path/to/signer-a', '-rpcwallet=authority-a'],
    [bitcoin_cli, '-regtest', '-rpcport=19444', '-datadir=/path/to/signer-b', '-rpcwallet=authority-b'],
    [bitcoin_cli, '-regtest', '-rpcport=19445', '-datadir=/path/to/signer-c', '-rpcwallet=authority-c'],
]
# Any accessible Bitcoin Core daemon can run finalizepsbt.
finalizer_cli = [bitcoin_cli, '-datadir=/path/to/purity-signet']

params = json.loads(params_path.read_text())
keys = params['public_keys']
if len(keys) != 3 or any(len(bytes.fromhex(key)) != 33 for key in keys):
    raise SystemExit('The configuration must contain three compressed public keys')
challenge = b'\x52' + b''.join(b'\x21' + bytes.fromhex(key) for key in keys) + b'\x53\xae'
template = {
    'height': 1,
    'coinbasevalue': 50 * 100_000_000,
    'previousblockhash': '00' * 32,
    'version': 1,
    'curtime': 1_700_000_000,
    'mintime': 1_700_000_000,
    'bits': '207fffff',
    'transactions': [],
    'signet_challenge': challenge.hex(),
}
miner = runpy.run_path('contrib/signet/miner', run_name='signer_preflight')
unsigned_psbt = miner['generate_psbt'](template, b'\x51')


def rpc(argv, method, *args):
    lines = [arg if isinstance(arg, str) else json.dumps(arg, separators=(',', ':')) for arg in args]
    result = subprocess.run(argv + ['-stdin', method], input='\n'.join(lines) + '\n',
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f'{method} failed (exit {result.returncode}); inspect the endpoint locally')
    return json.loads(result.stdout)


def partial_signature_pubkeys(psbt):
    decoded = rpc(finalizer_cli, 'decodepsbt', psbt)
    return [pubkey.lower() for pubkey in decoded['inputs'][0].get('partial_signatures', {})]


for first, second in itertools.combinations(range(3), 2):
    psbt = unsigned_psbt
    for index in (first, second):
        signed = rpc(signer_clis[index], 'walletprocesspsbt', psbt,
                     {'sign': True, 'sighashtype': 'ALL', 'bip32derivs': False, 'finalize': False})
        psbt = signed['psbt']
        partial_keys = partial_signature_pubkeys(psbt)
        expected_key = keys[index].lower()
        shown_keys = ', '.join(partial_keys) if partial_keys else 'none'
        print(f'Signer {chr(65 + index)}: walletprocesspsbt.complete={signed["complete"]}, '
              f'partial_signatures={len(partial_keys)}, configured_key_signed={expected_key in partial_keys}, '
              f'signing_public_keys={shown_keys}')
    finalized = rpc(finalizer_cli, 'finalizepsbt', psbt, False)
    if not finalized.get('complete'):
        partial_keys = partial_signature_pubkeys(psbt)
        shown_keys = ', '.join(partial_keys) if partial_keys else 'none'
        raise SystemExit(f'FAIL: signers {chr(65 + first)}+{chr(65 + second)} did not supply a complete 2-of-3 solution; '
                         f'partial_signatures={len(partial_keys)}, signing_public_keys={shown_keys}')
    print(f'PASS: signers {chr(65 + first)}+{chr(65 + second)}')
print('PASS: all three signer pairs completed; proceed to the final pre-freeze checks.')
PY
```

Replace all placeholder paths. For wallets on different hosts, use the matching
RPC connection arguments and protected authentication configuration; do not put
passwords in command arguments. Require a PASS for each pair and the final
summary. `walletprocesspsbt.complete` may remain false because finalization was
disabled. Check that each configured key supplied a signature, that the partial
signature count increases from one to two, and that `finalizepsbt.complete` is
true. Diagnostic public keys contain no private material. Do not share PSBTs,
private keys, recovery material or private descriptors.

The Purity data directory's `bitcoin.conf` already selects `chain=signet`.
Do not add `-signet` to CLI calls using that configuration: combining those two
network selectors is rejected.

This preflight checks signature completion for the configured challenge. It
does not test an actual GBT, PoW or first-block acceptance. Perform those checks
after freezing H, using section 5's `--once` run. Do not set `simulation=false`
before the real first block is accepted and verified.

#### B. Check public Purity P2P endpoints

Each seed peer must finish inherited-history synchronization, listen for P2P
connections and allow inbound TCP 38333 through its firewall and cloud security
group. RPC 38332 remains restricted to management access. On each peer, check:

```sh
build/bin/bitcoin-cli -datadir=/path/to/purity-signet getblockchaininfo
```

Require `chain=signet`, `initialblockdownload=false`, equal `headers` and
`blocks`, and a `bestblockhash` matching the fully validated active tip.
From a different external host or network, replace the example address and run:

```sh
nc -vz SEED_PUBLIC_IP 38333
```

Then connect from an external Purity probe node using the same parameters:

```sh
build/bin/bitcoin-cli -datadir=/path/to/probe addnode SEED_PUBLIC_IP:38333 onetry
build/bin/bitcoin-cli -datadir=/path/to/probe getpeerinfo
```

Confirm a connection to that address. During preparation, all nodes must be
fully synchronized; compare their heights and tip hashes. Once H exists and
the peers have exchanged valid post-fork block evidence, check
`signet_chain_verified=true`. An open TCP port or successful DNS lookup alone
does not establish Purity chain compatibility.

If DNS seeding is configured, check that the service-filtered name queried by
the client returns known Purity peer addresses:

```sh
dig +short A x8000009.seed.example.org
dig +short AAAA x8000009.seed.example.org
```

Replace `seed.example.org` with a configured `dns_seeds` name. Returned
addresses must serve reachable Purity peers on the default P2P port 38333.
`fixed_seed_nodes` uses actual numeric IP addresses and ports, not loopback.
If DNS or fixed seeds are not operated yet, their arrays can remain empty;
new nodes still need a reachable Purity peer through an explicit connection
or operated discovery endpoints. The copied configuration disables DNS
seeding; enable it explicitly if the deployment is intended to query DNS seeds.

After these checks, stop other imports and RPC operations that could change the
active tip, then run `prepare.py` to freeze T/H.

## 3. Freeze T and generate fixed parameters immediately before mining

Keep miners stopped and run:

```sh
python3 contrib/purity-signet/prepare.py \
  --config /path/to/purity-signet/signet-params.json \
  --output /path/to/purity-signet/fixed-params.json \
  --node-cli '["/absolute/path/to/bitcoin-cli","-datadir=/path/to/purity-signet","-rpcclienttimeout=0"]' \
  --ready
```

`--ready` confirms that the operator has checked the independent signers and
network endpoints. Use an absolute path to the matching `bitcoin-cli` build.
The tool:

1. Checks the synchronized preparation state and pauses networking.
2. Runs `verifychain 2 0` over all stored blocks and undo data, then
   `verifychain 4 288` over recent chainstate. It requires both calls to
   succeed and the frozen tip to remain unchanged.
3. Derives H=T+1, the parent hash, the H-5 anchor hash/bits, the anchor parent's
   hash/time and T's accumulated chainwork from the same validated snapshot.
4. Writes `fixed-params.json.snapshot.json`, requests normal shutdown and
   emits the fixed parameter file.

Full historical contextual and script validation comes from completed unpruned
synchronization with `assumevalid=0`. The level-2 scan checks stored history and
undo data; the bounded level-4 pass checks recent UTXO disconnect/reconnect
consistency without building a history-sized overlay. A completed lower-level
scan is not evidence that a skipped level-4 check succeeded. If either call
fails, keep mining stopped and investigate; do not bypass the check or keep
increasing `dbcache` to attempt full-history level 4.

By default the final snapshot is refused if its parent is over one hour old.
`--max-parent-age` changes only that operational limit, not consensus, timestamps
or bits. The disk scan may still take substantial time, so the example disables
the CLI RPC timeout. A stale-parent failure requires restoring networking and
resynchronizing before another freeze attempt.

The output and evidence paths must not already exist. A failed freeze can leave
networking paused. Inspect the failure first; to resume preparation safely, run:

```sh
build/bin/bitcoin-cli -datadir=/path/to/purity-signet setnetworkactive true
```

Successful tool exit does not mean the daemon has finished stopping. Wait for
clean process exit, then back up the entire stopped data directory to a separate
location together with its preparation configuration. This backup is required
to choose another T/H if launch is delayed before the first block.

## 4. Load the fixed parameters using the same data directory

The generated file still has `simulation=true` and `activation_hash=null`,
permitting the first block. Start:

```sh
build/bin/bitcoind -datadir=/path/to/purity-signet \
  -signetparams=/path/to/purity-signet/fixed-params.json
```

In another terminal, check:

```sh
build/bin/bitcoin-cli -datadir=/path/to/purity-signet \
  -signetparams=/path/to/purity-signet/fixed-params.json getblockchaininfo
build/bin/bitcoin-cli -datadir=/path/to/purity-signet \
  -signetparams=/path/to/purity-signet/fixed-params.json \
  getblocktemplate '{"rules":["segwit","signet"]}'
```

Require state `fixed`, GBT height H, `previousblockhash` equal to frozen T and
the ordered 2-of-3 challenge matching the public keys. Use the actual GBT bits.

Inactive conflicting original-chain branches and early headers received during
preparation do not by themselves prevent database startup. Relevant branches
are rechecked and incompatible ones excluded. An active history that does not
match the parent or anchor pins must fail startup. Do not delete database files
to bypass this failure.

## 5. Configure the independent miner and produce only the first block

Copy the miner sample to the operational data/configuration directory outside
the source checkout:

```sh
cp contrib/purity-signet/miner-simulation.json /path/to/purity-signet/miner.json
```

Edit `miner.json`:

- `chain_params`: the absolute path to this launch's `fixed-params.json`.
- `node_cli`: the CLI argument array for the fixed-parameter node, with its
  correct data directory and any RPC port override.
- `signer_clis`: two different signing wallets' CLI argument arrays, with their
  own data directories, wallet names and necessary RPC connection arguments.
- `bitcoin_util`: the absolute path to the matching `bitcoin-util` build.
- `reward_script`: the actual payout address's hexadecimal scriptPubKey;
  replace the simulation payout script.
- `poll_seconds`: an integer polling interval from 1 through 60 seconds.

Never put keys, wallet passphrases or RPC passwords in this file. The two wallets
must sign for different keys in the fixed challenge. Signer regtest addresses
identify authority keys; obtain the payout script from a wallet on the Purity
Signet chain. Run once:

```sh
python3 contrib/purity-signet/miner.py \
  --config /path/to/purity-signet/miner.json --once
```

The miner waits until the template time reaches parent time plus 600 seconds,
obtains two signatures, and changes only the nonce to solve actual PoW. It
checks the tip before solving and again before submission. A signer outage
never authorizes an unsigned submission. Current mining uses `bitcoin-util
grind`; this guide does not configure a Signet Stratum or DATUM signing service.

Status on stderr shows waiting countdowns, signer requests, quorum/proposal
checks, elapsed PoW, submission and acceptance. Stdout contains the accepted
block JSON. A wait or a long PoW calculation does not imply the process has hung.

## 6. Pin the first block and start continuous operation

After H is accepted, verify its actual hash, parent, signatures, PoW, required
difficulty, transactions and witness commitment, and check the chain state.
Archive the raw block, fixed parameters, snapshot evidence and file SHA256s.

Create a new `official-params.json`, changing only `activation_hash` to H's
actual hash and `simulation` to false. Retain H, the parent, public keys/order,
anchor and other consensus parameters.

Stop the node normally:

```sh
build/bin/bitcoin-cli -datadir=/path/to/purity-signet stop
```

Wait for process exit, then restart with the official file:

```sh
build/bin/bitcoind -datadir=/path/to/purity-signet \
  -signetparams=/path/to/purity-signet/official-params.json
```

Check the activation pin through RPC. Use independent test copies for full
`-reindex`, `-reindex-chainstate` and fresh-node synchronization acceptance.
All must reproduce the same H block and consistent UTXO results. Once fixed,
the official first-block pin cannot be removed or replaced.

Update the miner's `chain_params` to the official file and start continuous
mining:

```sh
python3 contrib/purity-signet/miner.py \
  --config /path/to/purity-signet/miner.json
```

After clean shutdown, also update the node's operational `bitcoin.conf` to the
official parameter path before relying on future starts without an explicit
`-signetparams` option. Confirm node RPC and miner configuration agree.

Only after verification should public Purity peers be enabled. Original peers
can supply shared history, but the post-fork network needs operated Purity peers.
`getpeerinfo.signet_chain_verified` records chain-data evidence; it does not
authenticate the operator. Monitor block height, hash/time, signer availability
and template/submission errors.

## 7. Delays and failure recovery

- If H has not been mined and launch is delayed, keep mining stopped. Restore
  the stopped preparation backup into an independent directory, resume sync and
  choose a new T/H. Do not change H online or casually replace it in a database
  that already contains the fixed-rule fingerprint.
- The parent-age check runs during freezing. Recheck it after a delayed restart
  or signing attempt. Do not falsify timestamps or override bits to hide a gap.
  ASERT retains H-5 as anchor, 600-second spacing and an 86400-second half-life.
- For signer failure, restore the same authority's wallet backup or select the
  other two available signers and restart the miner. Do not replace existing
  public keys or their order.
- For a pin mismatch, stop mining, preserve configuration/logs and check the
  actual history before modifying storage. Do not delete indexes first.
- An older fixed-rule database with active H+ blocks may require explicit
  `-reindex-chainstate`. Back up first and follow the startup diagnostics.

Official H comes from the actual frozen snapshot, not a height preselected by
this document. These instructions describe deployment steps, not completed
live-network acceptance. See the [acceptance record](purity-signet-acceptance.md)
for the scope of recorded validation.
