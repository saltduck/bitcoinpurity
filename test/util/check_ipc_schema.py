#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license.
"""Verify the IPC wire schemas against Bitcoin Core v32.1 (f0de87f040621468b956f01f526a43a010310501)."""
import hashlib
from pathlib import Path
import re

EXPECTED = {
    'common': '2d21b544359c22fcea6839ad47a0207269fae72dcdc1ffc80ce9838c26d8ab90',
    'echo': '59847b513f828b8f3c3c5ee7f281f027aaa1ee32f1b0046b5055a8af605f4dc7',
    'init': 'ddfeedb853df01f707efe6f3c69281dd39645c2987872714532dc39b8f63808a',
    'mining': '56bf0b3601cbe4a690d173ad7f1d259989a52e96b8937a6cd6180714b4864fea',
    'rpc': '331e1eca62db04a66dc6ac10bb7468b3e669737e4e65c7075c853f0b0db28889',
}
root = Path(__file__).resolve().parents[2] / "src/ipc/capnp"
for name, expected in EXPECTED.items():
    source = (root / (name + ".capnp")).read_text()
    source = re.sub(r"#[^\n]*", "", source)
    source = re.sub(r"\$Proxy\.\w+\([^)]*\)", "", source)
    canonical = re.sub(r"\s+", "", source)
    actual = hashlib.sha256(canonical.encode()).hexdigest()
    assert actual == expected, f"{name}: wire contract differs from Core v32.1"
print("All five IPC wire schemas match Bitcoin Core v32.1.")
