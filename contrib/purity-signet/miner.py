#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Sign a Signet template with two wallets, then solve its actual PoW."""

import argparse
import json
import logging
from pathlib import Path
import runpy
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "test/functional"))
from test_framework.messages import CBlockHeader, from_hex  # noqa: E402
from test_framework.psbt import PSBT, PSBT_GLOBAL_UNSIGNED_TX, PSBT_IN_NON_WITNESS_UTXO  # noqa: E402
from test_framework.script import CScriptOp  # noqa: E402

PRIMITIVES = runpy.run_path(str(ROOT / "contrib/signet/miner"), run_name="signet_primitives")
LOGGER = logging.getLogger("purity_signet_miner")


def rpc(argv, method, *args):
    arguments = [json.dumps(a, separators=(",", ":")) if not isinstance(a, str) else a for a in args]
    result = subprocess.run(argv + ["-stdin", method], input="\n".join(arguments) + ("\n" if arguments else ""),
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"RPC {method} failed (exit {result.returncode})")
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError:
        return result.stdout.strip() or None


class Miner:
    def __init__(self, path):
        config = json.loads(path.read_text())
        if not isinstance(config, dict) or type(config.get("schema_version")) is not int or config["schema_version"] != 1:
            raise ValueError("Unsupported miner configuration schema")
        self.node = config["node_cli"]
        self.signers = config["signer_clis"]
        if not isinstance(self.signers, list) or len(self.signers) != 2 or self.signers[0] == self.signers[1]:
            raise ValueError("Select two distinct signing wallets")
        for argv in [self.node, *self.signers]:
            if not isinstance(argv, list) or not argv or not all(isinstance(a, str) and a for a in argv):
                raise ValueError("CLI commands must be nonempty argument arrays")
        self.util = config["bitcoin_util"]
        self.reward = bytes.fromhex(config["reward_script"])
        self.poll = config.get("poll_seconds", 5)
        if not self.reward or not isinstance(self.poll, int) or not 1 <= self.poll <= 60:
            raise ValueError("Invalid payout script or polling interval")
        params_path = Path(config["chain_params"])
        if not params_path.is_absolute():
            params_path = path.parent / params_path
        self.params = json.loads(params_path.read_text())
        if self.params["activation_height"] is None:
            raise ValueError("Signet is preparing; freeze activation parameters before mining")
        for field in ("parent_hash", "activation_hash", "minimum_chain_work"):
            if self.params[field] is not None:
                self.params[field] = bytes.fromhex(self.params[field]).hex()
        for field in ("hash", "bits", "parent_hash"):
            self.params["anchor"][field] = bytes.fromhex(self.params["anchor"][field]).hex()
        self.params["public_keys"] = [bytes.fromhex(key).hex() for key in self.params["public_keys"]]

    def attempt(self):
        info = rpc(self.node, "getblockchaininfo")
        if info["chain"] != "signet" or "signet_fork" not in info:
            raise RuntimeError("Node is not the configured Purity Signet chain")
        fork = info["signet_fork"]
        anchor = self.params["anchor"]
        for actual, expected in (
            (fork["simulation"], self.params["simulation"]),
            (fork["minimum_chain_work"], self.params["minimum_chain_work"]),
            (fork["pow_limit"], "00000377ae000000000000000000000000000000000000000000000000000000"),
            (fork["activation_height"], self.params["activation_height"]),
            (fork["parent_hash"], self.params["parent_hash"]),
            (fork["anchor_height"], anchor["height"]),
            (fork["anchor_hash"], anchor["hash"]),
            (fork["anchor_bits"], anchor["bits"]),
            (fork["anchor_parent_hash"], anchor["parent_hash"]),
            (fork["anchor_parent_time"], anchor["parent_time"]),
            (fork["target_spacing"], 600), (fork["half_life"], 86400),
            (fork["message_start"], "0a03cf40"),
            (fork["activation_hash"], self.params["activation_hash"] or "0" * 64),
        ):
            if actual != expected:
                raise RuntimeError("Node and miner chain parameters differ")
        challenge = "52" + "".join("21" + key for key in self.params["public_keys"]) + "53ae"
        if fork["postfork_challenge"] != challenge:
            raise RuntimeError("Node and miner signing authority differ")
        template = rpc(self.node, "getblocktemplate", {"rules": ["segwit", "signet"]})
        if template["height"] < self.params["activation_height"]:
            raise RuntimeError("Historical Signet prefix is not synchronized")
        if template["signet_challenge"] != challenge:
            raise RuntimeError("Template challenge differs from configured authority")
        parent = rpc(self.node, "getblockheader", template["previousblockhash"])
        if template["curtime"] < parent["time"] + 600:
            LOGGER.info("Waiting for next block: height=%d remaining=%ds", template["height"],
                        parent["time"] + 600 - template["curtime"])
            return None
        LOGGER.info("Preparing block: height=%d parent=%s bits=%s", template["height"],
                    template["previousblockhash"], template["bits"])
        unsigned = PRIMITIVES["generate_psbt"](template, self.reward)
        original = PSBT.from_base64(unsigned)
        signed = unsigned
        for index, signer in enumerate(self.signers, start=1):
            LOGGER.info("Signing request: height=%d signer=%d/%d", template["height"], index, len(self.signers))
            signed = rpc(signer, "walletprocesspsbt", signed,
                         {"sign": True, "sighashtype": "ALL", "bip32derivs": False, "finalize": False})["psbt"]
            parsed = PSBT.from_base64(signed)
            for field in (PSBT_GLOBAL_UNSIGNED_TX, PRIMITIVES["PSBT_SIGNET_BLOCK"]):
                if parsed.g.map.get(field) != original.g.map[field]:
                    raise RuntimeError("Signing wallet changed the block or virtual transaction")
            if parsed.i[0].map.get(PSBT_IN_NON_WITNESS_UTXO) != original.i[0].map[PSBT_IN_NON_WITNESS_UTXO]:
                raise RuntimeError("Signing wallet changed the challenge transaction")
            LOGGER.info("Signing request complete: height=%d signer=%d/%d", template["height"], index, len(self.signers))
        LOGGER.info("Finalizing signatures: height=%d", template["height"])
        final = rpc(self.node, "finalizepsbt", signed, False)
        if not final["complete"]:
            raise RuntimeError("Two wallets did not provide the required signature quorum")
        LOGGER.info("Signature quorum complete: height=%d", template["height"])
        block, solution = PRIMITIVES["decode_psbt"](final["psbt"])
        block.vtx[0].vout[-1].scriptPubKey += CScriptOp.encode_op_pushdata(PRIMITIVES["SIGNET_HEADER"] + solution)
        if len(block.vtx[0].vout[-1].scriptPubKey) > 195:
            raise RuntimeError("Final commitment exceeds the consensus bound")
        block.vtx[0].rehash()
        block.hashMerkleRoot = block.calc_merkle_root()
        raw = block.serialize().hex()
        if rpc(self.node, "getbestblockhash") != template["previousblockhash"]:
            LOGGER.info("Discarding stale block before PoW: height=%d", template["height"])
            return None
        LOGGER.info("Checking signed proposal: height=%d", template["height"])
        rejection = rpc(self.node, "getblocktemplate", {"mode": "proposal", "data": raw})
        if rejection is not None:
            raise RuntimeError(f"Signed proposal rejected: {rejection}")
        header = CBlockHeader(block).serialize()
        LOGGER.info("PoW started: height=%d bits=%s solver=%s", template["height"], template["bits"], self.util)
        pow_started = time.monotonic()
        with subprocess.Popen([self.util, "grind", header.hex()], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True) as process:
            try:
                while True:
                    try:
                        solved_header, _ = process.communicate(timeout=self.poll)
                        break
                    except subprocess.TimeoutExpired:
                        LOGGER.info("PoW running: height=%d elapsed=%.0fs", template["height"],
                                    time.monotonic() - pow_started)
            except BaseException:
                process.kill()
                process.communicate()
                raise
        if process.returncode:
            raise RuntimeError("PoW solver failed")
        solved = from_hex(CBlockHeader(), solved_header.strip())
        if solved.serialize()[:76] != header[:76]:
            raise RuntimeError("PoW solver changed signed header fields")
        block.nNonce = solved.nNonce
        block.rehash()
        exponent, mantissa = block.nBits >> 24, block.nBits & 0x007fffff
        target = mantissa << (8 * (exponent - 3)) if exponent >= 3 else mantissa >> (8 * (3 - exponent))
        if block.sha256 > target:
            raise RuntimeError("PoW solver returned insufficient work")
        LOGGER.info("PoW complete: height=%d elapsed=%.0fs", template["height"], time.monotonic() - pow_started)
        if rpc(self.node, "getbestblockhash") != template["previousblockhash"]:
            LOGGER.info("Discarding stale block after PoW: height=%d", template["height"])
            return None
        LOGGER.info("Submitting block: height=%d hash=%s", template["height"], block.hash)
        rejection = rpc(self.node, "submitblock", block.serialize().hex())
        if rejection is not None:
            raise RuntimeError(f"Block rejected: {rejection}")
        if rpc(self.node, "getblockhash", template["height"]) != block.hash:
            LOGGER.warning("Submitted block is not active: height=%d hash=%s", template["height"], block.hash)
            return None
        LOGGER.info("Block accepted: height=%d hash=%s bits=%s", template["height"], block.hash, template["bits"])
        return {"height": template["height"], "hash": block.hash, "bits": template["bits"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--once", action="store_true", help="Exit after one accepted block")
    args = parser.parse_args()
    miner = Miner(args.config)
    LOGGER.info("Starting Signet miner: mode=%s poll=%ds solver=%s",
                "once" if args.once else "continuous", miner.poll, miner.util)
    while True:
        try:
            block = miner.attempt()
            if block is not None:
                print(json.dumps(block), flush=True)
                if args.once:
                    return
        except RuntimeError as error:
            if args.once:
                raise
            LOGGER.error("Attempt failed: %s; retrying in %ds", error, miner.poll)
        time.sleep(miner.poll)


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s",
                        datefmt="%Y-%m-%dT%H:%M:%S%z")
    try:
        main()
    except KeyboardInterrupt:
        LOGGER.info("Miner stopped")
        sys.exit(130)
    except (OSError, ValueError, TypeError, KeyError, RuntimeError) as error:
        LOGGER.error("%s", error)
        sys.exit(1)
