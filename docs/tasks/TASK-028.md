# TASK-028: Set default automatic parking depth to 3

Change `DEFAULT_PARK_REORG_DEPTH` from 6 to 3. Keep the strict boundary:
3-block rewinds may activate; rewinds of 4 or more are parked when parking is
enabled. Preserve explicit `-parkreorgdepth` overrides, the minimum of 1,
mainnet/test-chain enable defaults, persisted parking decisions, and consensus
rules.

Verification: `chainstatemanager_args` checks the effective default and
explicit overrides. `feature_park_deep_reorg.py` runs the default boundary
without an explicit depth argument and retains custom-depth and disabled cases.
