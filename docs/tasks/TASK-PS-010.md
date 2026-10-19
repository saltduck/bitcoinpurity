# TASK-PS-010: English deployment guide

Status: translated and locally verified; no deployment, commit or push.

Scope: translate the external Chinese deployment instructions into
[docs/purity-signet-deployment.md](../purity-signet-deployment.md), preserving
their signer setup, private-descriptor import, pair preflight, history sync,
late H freeze, mining, first-block pin and recovery steps. Leave the Chinese
original outside the repository and unchanged. Publish no operational secrets.

Align the older full-history level-4 instruction with current prepare.py:
verifychain(2,0) for stored history/undo and verifychain(4,288) for bounded recent
chainstate consistency. Keep the original build default and existing CLI/RPC
interfaces. Private-descriptor examples must not echo raw RPC failures.
No production code, dependency source, consensus, running process or operator
configuration changes are part of this task.

Verification passed: all seven numbered stages and 23 shell blocks are retained;
every shell block passes bash/zsh syntax checks and both embedded Python examples
parse. English content, public placeholders, local Markdown links and
git diff --check pass. The external Chinese source SHA256 remains unchanged.
No wallet, network or mining operations were executed.
