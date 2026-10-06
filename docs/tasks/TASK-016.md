# TASK-016: Defer active-chain manual parking and align tests

Status: implemented and locally verified (13/13 functional variants and the
default combined parking script passed; Python compilation/diff checks passed).

User decision: defer active-chain manual parking/rewind. Preserve production
behavior and all supported parking invariants.

- Replace the upstream positive active-park scenario with rejection of an
  active ancestor and tip, verifying unchanged tip and chain-tip status.
- Keep A/C orderings and adapt B/D to an inactive header branch whose bodies
  arrive after parking. Check state and activation after each operation.
- Retain C++ state permutations, automatic deep-reorg, descendant, restart,
  batch and import/rebuild coverage. No skip/xfail or production change.
- Update runner registration and provenance classifications. Preserve the
  original failing evidence as historical results, not current failures.

Evidence: [upstream parking test report](../../doc/parking-upstream-test-port.md).
