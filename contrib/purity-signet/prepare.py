#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Freeze a fully validated preparation tip before starting signed mining."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

RECENT_STATE_VERIFY_DEPTH = 288


def rpc(command, method, *args):
    values = [a if isinstance(a, str) else json.dumps(a) for a in args]
    result = subprocess.run(command + ["-stdin", method], input="\n".join(values) + ("\n" if values else ""), capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"RPC {method} failed (exit {result.returncode}); networking remains paused if freezing began")
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError:
        return result.stdout.strip()


def freeze(config, call, max_parent_age, now):
    if config.get("schema_version") != 2 or any(config.get(k, False) is not None for k in ("activation_height", "parent_hash", "anchor", "activation_hash")):
        raise ValueError("Use an explicit schema v2 preparation configuration")
    before = call("getblockchaininfo")
    fork = before.get("signet_fork", {})
    challenge = "52" + "".join("21" + key.lower() for key in config["public_keys"]) + "53ae"
    if before["chain"] != "signet" or fork.get("state") != "preparing" or fork.get("postfork_challenge") != challenge:
        raise ValueError("Node preparation state or signing keys differ from the configuration")
    if before["initialblockdownload"] or before["headers"] != before["blocks"] or before["blocks"] < 5:
        raise ValueError("Node must finish validating all headers before freezing")
    if before["pruned"]:
        raise ValueError("Preparation requires unpruned history for the full block/undo scan and recovery")
    call("setnetworkactive", False)
    if call("getnetworkinfo")["networkactive"]:
        raise ValueError("Could not pause networking")
    snapshot = call("getblockchaininfo")
    if snapshot["initialblockdownload"] or snapshot["headers"] != snapshot["blocks"]:
        raise ValueError("Chain changed while freezing; finish synchronization and retry")
    if not call("verifychain", 2, 0):
        raise ValueError("Full stored block and undo verification failed")
    if not call("verifychain", 4, RECENT_STATE_VERIFY_DEPTH):
        raise ValueError("Recent chainstate verification failed")
    height = snapshot["blocks"] + 1
    parent = call("getblockheader", snapshot["bestblockhash"])
    anchor_hash = call("getblockhash", height - 5)
    anchor = call("getblockheader", anchor_hash)
    anchor_parent = call("getblockheader", anchor["previousblockhash"])
    now = int(now()) if callable(now) else now
    if now - parent["time"] > max_parent_age:
        raise ValueError("Parent is older than the configured maximum age; leave mining stopped and resynchronize")
    if (parent["height"] != height - 1 or parent["hash"] != snapshot["bestblockhash"] or
            parent["chainwork"] != snapshot["chainwork"] or anchor["height"] != height - 5 or
            anchor["hash"] != anchor_hash or anchor_parent["height"] != height - 6 or
            anchor_parent["hash"] != anchor["previousblockhash"]):
        raise ValueError("Frozen parent/anchor snapshot is inconsistent")
    end = call("getblockchaininfo")
    if end["bestblockhash"] != parent["hash"] or end["headers"] != snapshot["headers"] or call("getnetworkinfo")["networkactive"]:
        raise ValueError("Frozen chain changed during verification; do not start mining")
    result = dict(config, simulation=True, activation_height=height, parent_hash=parent["hash"], activation_hash=None,
                  minimum_chain_work=parent["chainwork"], anchor={"height": anchor["height"], "hash": anchor_hash,
                  "bits": anchor["bits"], "parent_hash": anchor_parent["hash"], "parent_time": anchor_parent["time"]})
    evidence = {"schema_version": 2, "frozen_at": now, "max_parent_age": max_parent_age,
                "chain": snapshot, "parent": parent, "anchor": anchor, "anchor_parent": anchor_parent,
                "verifychain": {
                    "full_history": {"level": 2, "depth": 0, "checked_blocks": snapshot["blocks"], "result": True},
                    "recent_chainstate": {"level": 4, "depth": RECENT_STATE_VERIFY_DEPTH,
                                          "checked_blocks": min(RECENT_STATE_VERIFY_DEPTH, snapshot["blocks"]), "result": True},
                },
                "networkactive": False}
    return result, evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--node-cli", required=True, help="JSON array of bitcoin-cli arguments, using local cookie authentication")
    parser.add_argument("--ready", action="store_true", help="Confirm independent signing wallets and network endpoints are ready")
    parser.add_argument("--max-parent-age", type=int, default=3600, help="Operational age limit in seconds, not a consensus rule")
    args = parser.parse_args()
    if not args.ready or args.max_parent_age < 0:
        parser.error("Confirm readiness with --ready and use a nonnegative parent age limit")
    command = json.loads(args.node_cli)
    if not isinstance(command, list) or not command or not all(isinstance(a, str) and a for a in command):
        parser.error("--node-cli must be a nonempty JSON argument array")
    evidence_path = args.output.with_suffix(args.output.suffix + ".snapshot.json")
    if args.output.exists() or evidence_path.exists():
        parser.error("Output or snapshot already exists; never overwrite a frozen configuration")
    source = args.config.read_bytes()
    result, evidence = freeze(json.loads(source), lambda method, *values: rpc(command, method, *values), args.max_parent_age, time.time)
    encoded = (json.dumps(result, indent=2) + "\n").encode()
    evidence["preparation_sha256"] = hashlib.sha256(source).hexdigest()
    evidence["fixed_config_sha256"] = hashlib.sha256(encoded).hexdigest()
    # Keep evidence even if shutdown fails; no launch file is issued in that case.
    with evidence_path.open("x") as output:
        json.dump(evidence, output, indent=2)
        output.write("\n")
    rpc(command, "stop")
    with args.output.open("xb") as output:
        output.write(encoded)
    print(f"Fixed H={result['activation_height']}; stop requested. Wait for clean process exit and back up the datadir before restart.")
    print(f"Configuration: {args.output}\nEvidence: {evidence_path}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, OSError, KeyError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
