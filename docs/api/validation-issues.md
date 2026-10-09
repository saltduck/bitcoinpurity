# Validation RPC and test configuration contract

Configuration fee rates use coin/kvB: `fallbackfee=0.00001`,
`minrelaytxfee=0.000001`, `incrementalrelayfee=0.0000001` and
`blockmintxfee=0.000004`. These are respectively 1, 0.1, 0.01 and 0.4 sat/vB.
`blockmaxsize` defaults to 3985000 bytes and `blockmaxweight` to 3985000
weight units. Mainnet `assumevalid` defaults to
`00000000000000000000807f9dc917442a67910426d79ebb2f8aa2149327ce8a`
(height 961631). Explicit values override these defaults; `corepolicy=1`
continues to select its existing fee and block-template profile. RPC units
and fields are unchanged. Wallet `mintxfee` remains 1 sat/vB.

`-maxtipage=<n>` defaults to 604800 seconds (7 days), previously 86400.
An explicit value in the configuration file or command line still overrides
the default. `getblockchaininfo.initialblockdownload` uses this age threshold
alongside its existing conditions. RPC fields and consensus rules are unchanged.

`getchaintips` returns an additional required boolean `parked` on every tip.
It covers the tip and ancestors with `BLOCK_PARKED_MASK`; existing fields and
status values are unchanged. `unparkblock HASH` clears relevant parking flags
and invokes ordinary `ActivateBestChain(nullptr)` with no special approval
mode. `reconsiderblock` clears invalidity but preserves parking; it does not
perform a new acceptance-time depth decision for already stored bodies.
Enabling parking on restart does not retroactively classify every stored branch.

`parkblock HASH` supports inactive branches only. An active-chain target returns
RPC error -1 without changing the tip or parking state. Active-chain manual
parking/rewind is deferred; tests cover rejection for both ancestor and tip.

Regtest-only `-testactivationheight=purity@H` configures permanent RDTS (with
fixed regtest difficulty). `-testactivationheight=rdtsgrandfatherfix@H` configures
the grandfather correction separately. Heights use the existing validation
range `[0, INT_MAX)`. Mainnet has no runtime correction override.
Mainnet hardcodes both Purity activation and grandfather correction to 961637;
the regtest switches do not change public-network parameters.

Remote signed manifest schemas are unchanged, but any duplicate object key
at any depth is invalid before signature verification or semantic use. Release
artifact URLs additionally enforce the official GitHub download namespace and
check all redirects. See `doc/software-updates.md` for the exact URI policy.
