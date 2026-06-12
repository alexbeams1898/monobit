#!/usr/bin/env python3
"""Dialog register linter for Selva Oscura.

Walks config/npcs/*.json and checks each topic's NPC speech ("line")
for modern-register words that shouldn't appear in old-register text,
plus each choice's player-intent text ("label") for archaic words
that shouldn't appear in modern-register player speech.

Per dialogue.md register doctrine:
  - NPC speech is OLD register (Italian loanwords, Early Modern English).
  - Player choices are MODERN register (plain English, the player's voice).

This catches the most common drift cases. Lists are intentionally
small; expand when drift is caught in playtest.

Exit code 0 on pass, 1 on any violation.
"""

import json
import re
import sys
from pathlib import Path

# Modern-register words that should NOT appear in NPC lines.
# Lowercase form; the scanner normalizes input.
MODERN_BANNED_IN_NPC = [
    "you're", "i'm", "we're", "they're",
    "can't", "won't", "don't", "isn't", "didn't", "wasn't", "wouldn't",
    "couldn't", "shouldn't", "haven't", "hasn't", "hadn't", "doesn't",
    "gonna", "wanna", "gotta",
    "yeah", "yep", "nope", "ok", "okay",
    "dude", "guy", "guys", "buddy",
    "stuff", "things",
    "kinda", "sorta",
    "right?", "y'know",
    "you guys", "you all",
]

# Archaic-register words that should NOT appear in player choice labels.
ARCHAIC_BANNED_IN_PLAYER = [
    "thou", "thee", "thy", "thine",
    "ye",
    "hath", "doth", "dost", "wilt", "shalt",
    "wast", "wouldst", "couldst", "shouldst",
    "verily", "forsooth", "mayhap",
    "betwixt", "amongst",
    "ere",
    "naught", "aught",
]


def scan_text(text: str, banned: list[str]) -> list[str]:
    """Return list of banned words found in text (case-insensitive)."""
    lower = text.lower()
    hits = []
    for word in banned:
        # For multi-word phrases, just substring-search.
        if " " in word or word.endswith(" "):
            if word in lower:
                hits.append(word.strip())
            continue
        # For single words, use word boundaries so "art" doesn't match
        # "started" or "thou" doesn't match "though".
        pattern = r"\b" + re.escape(word) + r"\b"
        if re.search(pattern, lower):
            hits.append(word)
    return hits


def check_file(path: Path) -> list[str]:
    """Return list of violation messages for one NPC JSON file."""
    violations = []
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        return [f"{path}: parse error: {e}"]

    npc_id = data.get("npc_id", "<unknown>")
    for topic in data.get("topics", []):
        topic_id = topic.get("id", "<unknown>")

        # NPC line: must not contain modern register.
        line = topic.get("line", "")
        if line:
            hits = scan_text(line, MODERN_BANNED_IN_NPC)
            if hits:
                violations.append(
                    f"{path}: npc={npc_id} topic={topic_id} NPC line contains "
                    f"modern-register: {sorted(set(hits))} | line: {line!r}"
                )

        # Player choice labels: must not contain archaic register.
        for choice in topic.get("choices", []):
            label = choice.get("label", "")
            choice_id = choice.get("id", "<unknown>")
            if label:
                hits = scan_text(label, ARCHAIC_BANNED_IN_PLAYER)
                if hits:
                    violations.append(
                        f"{path}: npc={npc_id} topic={topic_id} choice={choice_id} "
                        f"player label contains archaic-register: "
                        f"{sorted(set(hits))} | label: {label!r}"
                    )

    return violations


def main() -> int:
    repo_root = Path(__file__).resolve().parents[3]
    npc_dir = repo_root / "games" / "selva-oscura" / "config" / "npcs"
    if not npc_dir.is_dir():
        print(f"[dialog-register] no npc config dir at {npc_dir}; skipping", file=sys.stderr)
        return 0

    all_violations = []
    file_count = 0
    for path in sorted(npc_dir.glob("*.json")):
        file_count += 1
        all_violations.extend(check_file(path))

    if all_violations:
        print(f"[dialog-register] FAIL: {len(all_violations)} violation(s) across "
              f"{file_count} file(s)", file=sys.stderr)
        for v in all_violations:
            print(f"  {v}", file=sys.stderr)
        return 1

    print(f"[dialog-register] OK: {file_count} file(s) clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
