# TASK-031: Mining template and validation bridge

Test first: coinbase fields, resources, interruption, witness cache reset, submission results and batch transaction lookup.

See [requirements](../../input/requirements.md), [architecture](../architecture/embedded-sv2.md) and [contract](../api/sv2.md).

## Status

Pending implementation and validation.

## Witness validation cache backport (#33745)

The regression test first failed on the original implementation: a replacement
coinbase without witness was accepted and advanced the tip. Clearing all three
block validation caches after reconstruction makes the regression pass on
macOS arm64 (Release, wallet/GUI/DATUM disabled). This subtask does not complete
the template bridge or any gateway runtime acceptance.
