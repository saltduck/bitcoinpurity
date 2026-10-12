# Purity P2P and RPC service contract

Mainnet DNS bootstrap uses only `seed.bitcoinpurity.org`. This changes the
built-in seed list without changing RPC/P2P formats, DNS-seeding options or
explicit peer configuration. Other networks' DNS seeds and fixed seeds remain
unchanged.

Mainnet fixed-seed fallback contains only the 11 endpoints selected in
`input/requirements.md`. `node0.bitcoinpurity.org` is encoded as
`49.190.167.39` at ports 8333 and 8334; `node1.bitcoinpurity.org` is encoded as
`51.79.241.169:8333`, using DNS results from 2026-10-12. These are static address
snapshots; updating DNS alone does not change compiled fixed seeds.
The list retains the requested IPv6 endpoints and `57.129.53.78:5890`.
RPC/P2P formats and the `-fixedseeds` option retain their existing behavior.

`NODE_PURITY_ASERT` is experimental nServices bit 25. It claims support for
the Bitcoin Purity ASERT chain and is only an unauthenticated discovery hint.

`getnetworkinfo.localservicesnames` includes `PURITY_ASERT`.
`getpeerinfo.servicesnames` includes it exactly when the remote VERSION
services advertise bit 25. The numeric service mask retains its existing
encoding. Other known names and UNKNOWN[2^n] behavior are unchanged.

VERSION, addr, addrv2, network magic and message framing are unchanged.
Absence of bit 25 does not affect VERSION/VERACK acceptance. The existing
services/stale-peer checks and activation-block hash verification still apply.

An activation-block hash mismatch still rejects the received header with
`bad-purity-activation-block`. Its diagnostic is DEBUG in the validation
category (enabled with `-debug=validation` at debug log level), and includes
the height and received/pinned hashes without an `ERROR:` prefix. With the
category disabled, that diagnostic is suppressed. A conflicting activation
block already stored in the local block index remains an ERROR that prevents
startup and advises `-reindex`. RPC and P2P formats are unchanged.
