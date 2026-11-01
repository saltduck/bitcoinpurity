# TASK-032: Rust protocol gateway and lifecycle

Implement pinned SRI integration, authenticated Full-Template declaration, V1 conversion, bounded state and same-pool retry through C ABI.

See [requirements](../../input/requirements.md), [architecture](../architecture/embedded-sv2.md) and [contract](../api/sv2.md).

## Status

Pending implementation and validation.

## Authenticated transport checkpoint

Pinned SRI commit d1d30f3e755b0a5108633e1be7012034a8caaecd and Rust 1.96.0,
with a committed Cargo lockfile. Added cancellation-safe framed Noise transport,
bounded connection/write timeouts, and support for borrowed/owned SRI messages.
The independent client/server test verifies encrypted messages and rejection of
a different authority key. It first failed against the missing wire module.
