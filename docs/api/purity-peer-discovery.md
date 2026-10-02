# Purity P2P and RPC service contract

`NODE_PURITY_ASERT` is experimental nServices bit 25. It claims support for
the Bitcoin Purity ASERT chain and is only an unauthenticated discovery hint.

`getnetworkinfo.localservicesnames` includes `PURITY_ASERT`.
`getpeerinfo.servicesnames` includes it exactly when the remote VERSION
services advertise bit 25. The numeric service mask retains its existing
encoding. Other known names and UNKNOWN[2^n] behavior are unchanged.

VERSION, addr, addrv2, network magic and message framing are unchanged.
Absence of bit 25 does not affect VERSION/VERACK acceptance. The existing
services/stale-peer checks and activation-block hash verification still apply.
