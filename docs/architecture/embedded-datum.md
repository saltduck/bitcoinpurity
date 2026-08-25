# Embedded DATUM Architecture

## Stage 1 boundary

```text
External SHA256d miner (ASIC or GPU)
    | Stratum V1
    v
bitcoinpurityd
    |-- Purity node, mempool, BlockAssembler, validation, P2P
    `-- C++ datum bridge
          `-- upstream-derived DATUM C subsystem
                | localhost HTTP JSON-RPC
                v
          Purity getblocktemplate / submitblock
```

Purity-specific code is confined to the bridge, argument registration,
lifecycle hooks, validation notification, and optional status RPC. DATUM C must
not include Purity C++ headers. When `BUILD_DATUM=ON`, Bitcoin-Qt adds a DATUM
configuration tab alongside the unchanged upstream Settings surface. The tab
writes runtime values through `ArgsManager`; sensitive DATUM credentials remain
out of `settings.json`.

## Ownership and lifecycle

Purity owns construction, startup, interruption, stop, and destruction. Startup
occurs after RPC warmup finishes and chainstate is ready. Interruption first
closes the Stratum listener and wakes workers. Stop joins all DATUM-owned
threads before HTTP/RPC teardown.

When `datumupnp=1`, the C++ bridge owns a separate interruptible port-mapping
worker. It first uses miniupnpc to discover a valid IPv4 IGD, then falls back
to PCP/NAT-PMP when the router does not advertise an IGD. It maps only the
configured DATUM Stratum TCP port, renews the mapping, retries operational
failures, and removes the UPnP mapping during clean shutdown (PCP/NAT-PMP
leases expire naturally). This mapping is independent of the node's P2P
`-upnp` worker and is rejected for loopback or IPv6-only DATUM listeners.

An `UpdatedBlockTip` listener calls a C ABI refresh function. The function only
sets the same bounded refresh state used by the baseline SIGUSR1 handler; the
template thread continues to fetch GBT over localhost RPC and retains periodic
refresh.

The `setdatumdiff` RPC and the Qt share-difficulty field write the new share
difficulty to a C11 atomic runtime value. Each Stratum worker observes that
value in its own event loop and sends the updated difficulty and a clean current
job from the worker thread, avoiding cross-thread socket-buffer access. The
selected `datumdiff` is persisted for startup, and the runtime value never
changes the GBT network target.

## Dependency direction

```text
bitcoin_node -> datum_bridge (C++) -> datum_solo (C)
                                     -> curl
                                     -> jansson
                                     -> platform socket/thread primitives
                                     -> miniupnpc (when `WITH_MINIUPNPC=ON` and
                                        `datumupnp=1` is used)
                                     -> PCP/NAT-PMP (fallback)
```

Prime/protocol encryption and Web/API sources are excluded. `libmicrohttpd` is
therefore absent; libsodium remains only because the retained upstream utility
code uses its SHA-256 API. All DATUM-only dependencies are conditional on
`BUILD_DATUM` and are represented in CMake, depends, and vcpkg.

## Portability constraint

The baseline socket implementation is epoll/POSIX-specific and its daemon
threads are intentionally process-lifetime. The embedded implementation uses a
bounded `poll`/`WSAPoll` adapter plus small POSIX/Win32 thread and time wrappers,
alongside auditable lifecycle hooks. These adapters do not change Stratum
parsing or mining/share logic.

## Stage 2 seam

The bridge API may later replace HTTP JSON-RPC with C callbacks backed by node
interfaces. Stage 1 must not expose Purity C++ types to DATUM C or implement that
optimization.
