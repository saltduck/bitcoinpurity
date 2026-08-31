#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license.
"""Generate WoW-style adjective and noun word lists for DATUM coinbase tags."""

from __future__ import annotations

import pathlib
import sys

TARGET_COUNT = 2048
MAX_WORD_LEN = 20

BASE_ADJECTIVES = [
    "Swift", "Mighty", "Silent", "Fierce", "Brave", "Grim", "Wild", "Ancient",
    "Frost", "Shadow", "Storm", "Golden", "Crimson", "Azure", "Ember", "Sturdy",
    "Noble", "Cunning", "Valiant", "Bold", "Iron", "Crystal", "Lunar", "Solar",
    "Thunder", "Ghost", "Phantom", "Savage", "Mystic", "Sacred", "Hollow", "Primal",
    "Radiant", "Dusky", "Glacial", "Volcanic", "Obsidian", "Emerald", "Sapphire", "Amber",
    "Verdant", "Ashen", "Blazing", "Soaring", "Wandering", "Fearless", "Dauntless", "Righteous",
    "Bleak", "Dire", "Fell", "Stark", "Bitter", "Keen", "Proud", "Stern",
    "Hoary", "Misty", "Molten", "Frozen", "Warped", "Blessed", "Cursed", "Haunted",
]

BASE_NOUNS = [
    "Wolf", "Raven", "Bear", "Hawk", "Dragon", "Blade", "Storm", "Sentinel",
    "Guardian", "Warden", "Hunter", "Ranger", "Paladin", "Warrior", "Rogue", "Mage",
    "Druid", "Monk", "Shaman", "Knight", "Champion", "Stalker", "Reaver", "Fang",
    "Claw", "Talon", "Shield", "Hammer", "Spear", "Arrow", "Flame", "Frost",
    "Thunder", "Stone", "Oak", "River", "Mountain", "Valley", "Meadow", "Isle",
    "Comet", "Star", "Moon", "Spirit", "Wraith", "Titan", "Gryphon", "Phoenix",
    "Hound", "Stag", "Boar", "Lion", "Serpent", "Viper", "Eagle", "Owl",
    "Forge", "Anvil", "Crown", "Sigil", "Banner", "Relic", "Totem", "Idol",
]

ADJECTIVE_PREFIXES = [
    "Blood", "Dark", "Storm", "Frost", "Shadow", "Wild", "Ancient", "Grim",
    "Fell", "Dire", "Bleak", "Stark", "Bitter", "Burning", "Chilling", "Howling",
    "Roaring", "Whispering", "Raging", "Blessed", "Cursed", "Lone", "Proud", "Stern",
    "Hoary", "Misty", "Molten", "Frozen", "Deep", "High", "True", "Elder",
    "Death", "Soul", "Ash", "Bone", "Iron", "Gold", "Silver", "Star",
    "Moon", "Sun", "Void", "Chaos", "Sacred", "Profane", "Holy", "Haunted",
    "Fallen", "Risen", "Sleeping", "Hunting", "Hunted", "Twisted", "Blighted", "Radiant",
    "Dusky", "Glacial", "Volcanic", "Primal", "Hollow", "Mystic", "Phantom", "Ghost",
]

ADJECTIVE_SUFFIXES = [
    "born", "hearted", "souled", "marked", "touched", "bound", "wrought", "forged",
    "blooded", "eyed", "clad", "wise", "fell", "keen", "free", "bright",
    "dark", "pale", "red", "black", "white", "grey", "gold", "silver",
    "iron", "steel", "frost", "flame", "storm", "wind", "stone", "oak",
]

NOUN_PREFIXES = [
    "Blood", "Dark", "Storm", "Frost", "Shadow", "Wild", "Ancient", "Grim",
    "Ash", "Bone", "Iron", "Gold", "Silver", "Star", "Moon", "Sun",
    "Fire", "Ice", "Wind", "Stone", "Oak", "Thorn", "Moss", "Wolf",
    "Raven", "Bear", "Hawk", "Dragon", "Flame", "Thunder", "Ghost", "Spirit",
    "Death", "Soul", "Void", "Chaos", "Sacred", "Cursed", "Fallen", "Risen",
    "Hollow", "Crystal", "Obsidian", "Emerald", "Sapphire", "Amber", "Crimson", "Azure",
    "Ember", "Dusky", "Glacial", "Volcanic", "Radiant", "Misty", "Hoary", "Bleak",
    "Lone", "Deep", "High", "Elder", "Night", "Day", "Dream", "Doom",
]

NOUN_SUFFIXES = [
    "born", "kin", "fang", "claw", "ward", "walker", "rider", "caller",
    "seeker", "keeper", "bringer", "breaker", "maker", "bane", "heart", "soul",
    "blade", "shield", "hammer", "spear", "arrow", "flame", "frost", "storm",
    "stone", "rock", "wood", "bark", "leaf", "root", "wing", "horn",
]


def accept(word: str) -> bool:
    return 2 <= len(word) <= MAX_WORD_LEN and word.isascii() and word[0].isupper()


def combine(head: str, tail: str) -> str | None:
    if head.lower() == tail.lower():
        return None
    if tail.lower().startswith(head.lower()):
        return None
    return f"{head}{tail.lower()}"


def fill(words: set[str], candidates: list[str | None]) -> None:
    for word in candidates:
        if word and accept(word):
            words.add(word)


def generate_adjectives() -> list[str]:
    words: set[str] = set()
    fill(words, BASE_ADJECTIVES)
    for prefix in ADJECTIVE_PREFIXES:
        for base in BASE_ADJECTIVES:
            fill(words, [combine(prefix, base)])
    for base in BASE_ADJECTIVES:
        for suffix in ADJECTIVE_SUFFIXES:
            fill(words, [combine(base, suffix)])
    for prefix in ADJECTIVE_PREFIXES:
        for suffix in ADJECTIVE_SUFFIXES:
            fill(words, [combine(prefix, suffix)])
    ordered = sorted(words)
    if len(ordered) < TARGET_COUNT:
        raise RuntimeError(f"only generated {len(ordered)} adjectives")
    return ordered[:TARGET_COUNT]


def generate_nouns() -> list[str]:
    words: set[str] = set()
    fill(words, BASE_NOUNS)
    for prefix in NOUN_PREFIXES:
        for base in BASE_NOUNS:
            fill(words, [combine(prefix, base)])
    for base in BASE_NOUNS:
        for suffix in NOUN_SUFFIXES:
            fill(words, [combine(base, suffix)])
    for prefix in NOUN_PREFIXES:
        for suffix in NOUN_SUFFIXES:
            fill(words, [combine(prefix, suffix)])
    ordered = sorted(words)
    if len(ordered) < TARGET_COUNT:
        raise RuntimeError(f"only generated {len(ordered)} nouns")
    return ordered[:TARGET_COUNT]


def emit_array(name: str, words: list[str]) -> str:
    lines = [f"inline constexpr std::array<const char*, {len(words)}> {name}{{"]
    row: list[str] = []
    for word in words:
        row.append(f"\"{word}\"")
        if len(row) == 8:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    adjectives = generate_adjectives()
    nouns = generate_nouns()
    output = pathlib.Path(__file__).resolve().parents[2] / "src/qt/datumcoinbasetag_words.h"
    content = f"""// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
// Generated by contrib/devtools/generate_datumcoinbasetag_words.py — do not edit by hand.

#ifndef BITCOIN_QT_DATUMCOINBASETAG_WORDS_H
#define BITCOIN_QT_DATUMCOINBASETAG_WORDS_H

#include <array>

namespace DatumCoinbaseTagWords {{

{emit_array("WOW_ADJECTIVES", adjectives)}

{emit_array("WOW_NOUNS", nouns)}

}} // namespace DatumCoinbaseTagWords

#endif // BITCOIN_QT_DATUMCOINBASETAG_WORDS_H
"""
    output.write_text(content, encoding="utf-8")
    print(f"Wrote {output} ({len(adjectives)} adjectives, {len(nouns)} nouns)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
