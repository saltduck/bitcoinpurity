# TASK-015: Acceptance-time deep-reorg parking

Status: implemented and locally verified (19 C++ cases, 6 functional runs,
148 CTest tests passed).

Scope: parking only. Preserve RDTS, replay protection, signed manifests and all
consensus rules. Supersedes TASK-014's activation-time parking implementation.

Acceptance: root parked in `AcceptBlock` before candidate propagation;
thresholds 1/6/7/20 at depth 6 and 4/5 at depth 4; disabled policy; normal and
ancestor-last delivery; parked descendant exclusion; ordinary null-body unpark
activation; persisted flags across restart/reconsider/chainstate rebuild;
out-of-order import; explicit full-reindex reset behavior; additive RPC field.
No automatic unparking or special activation override.

Evidence and commands: [parking review](../../doc/deep-reorg-parking-review.md).
