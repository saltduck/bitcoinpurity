# TASK-036: Verified-only Purity peer preference (audit M-2)

Implement the [requirements](../../input/requirements.md) section
"Verified-only Purity peer preference" and its
[architecture](../architecture/purity-peer-discovery.md).

## Acceptance

- Gossip-only (new-table) bit-25 claims are not preferred; normal selection is
  used when no tried record matches.
- Tried bit-25 records are preferred through the ordinary tried bucket walk.
- Network filters, feelers (`new_only`) and the 20-attempt limit are unchanged.

## Validation

Test-first: `addrman_purity_preference_and_fallback` and the new
`addrman_purity_gossip_claims_not_preferred` failed on the original sources
(new-table claims were always selected). Both pass with the change, together
with the full unit suite, `p2p_purity_services.py`, `p2p_addr_relay.py`,
`p2p_addrv2_relay.py` and `feature_anchors.py`.
