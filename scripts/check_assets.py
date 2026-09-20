"""Checks that every asset a game declares exists, and that it will ship.

Two failures this catches, both of which have already happened here.

An asset path written in config that names nothing: selva's iron_buckler and
longsword have declared a `mesh` since they were written, and neither file has
ever existed on any branch. Nothing loaded the field, so nothing complained,
and it would have become a live bug the day something did.

An asset that exists and is not in the release manifest. Packaging is
inclusion-only, which fails closed -- the failure mode it trades for is
shipping a game missing a file it needs. That is only safe if something
compares the two lists, which is this. Without it, inclusion just moves the
silence from the zip to the player.
"""

from __future__ import annotations

import json
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import package  # noqa: E402
import releaseconfig  # noqa: E402

# Anything under a game's assets/ that the runtime might name.
ASSET_REF = re.compile(r"^assets/[^\"'\s]+\.[A-Za-z0-9]+$")

# A C++ string literal naming an asset.
CPP_LITERAL = re.compile(r"\"(assets/[^\"\\\s]+\.[A-Za-z0-9]+)\"")


def games(root: pathlib.Path) -> list[pathlib.Path]:
    """Game directories that declare a release config."""
    base = root / "games"
    if not base.is_dir():
        return []
    return sorted(d for d in base.iterdir()
                  if (d / releaseconfig.CONFIG_NAME).is_file())


def _json_strings(value: object):
    """Every string in a decoded JSON document, at any depth."""
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for v in value.values():
            yield from _json_strings(v)
    elif isinstance(value, list):
        for v in value:
            yield from _json_strings(v)


def declared_assets(game_dir: pathlib.Path) -> dict[str, list[pathlib.Path]]:
    """Asset paths the game names, mapped to the files that name them."""
    out: dict[str, list[pathlib.Path]] = {}

    def note(ref: str, where: pathlib.Path) -> None:
        out.setdefault(ref, []).append(where.relative_to(game_dir))

    for sub in ("config", "assets"):
        base = game_dir / sub
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*.json")):
            try:
                doc = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, ValueError):
                # A malformed or unreadable config is the JSON loader's
                # problem to report, with its own line numbers. Skipping it
                # here keeps this tool's errors about assets.
                continue
            for s in _json_strings(doc):
                if ASSET_REF.match(s):
                    note(s, path)

    src = game_dir / "src"
    if src.is_dir():
        for path in sorted(src.rglob("*")):
            if path.suffix not in (".cpp", ".h"):
                continue
            try:
                text = path.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            for m in CPP_LITERAL.finditer(text):
                note(m.group(1), path)

    return out


def check_game(game_dir: pathlib.Path) -> list[str]:
    problems: list[str] = []
    rel_game = game_dir.relative_to(REPO_ROOT)

    try:
        cfg = releaseconfig.load(game_dir)
    except releaseconfig.ConfigError as e:
        return [f"{rel_game}: {e}"]

    include = cfg.get("include")
    if not isinstance(include, list) or not include:
        return [f"{rel_game}/{releaseconfig.CONFIG_NAME}: no `include:` list"]

    shipped = package.manifest_files(game_dir, include)

    for ref, sources in sorted(declared_assets(game_dir).items()):
        on_disk = game_dir / ref
        where = ", ".join(str(s) for s in sorted(set(sources))[:3])
        if not on_disk.is_file():
            problems.append(f"{rel_game}: {ref} does not exist (named by {where})")
        elif pathlib.Path(ref) not in shipped:
            problems.append(
                f"{rel_game}: {ref} exists but is not covered by `include:` in "
                f"{releaseconfig.CONFIG_NAME}, so it would not ship "
                f"(named by {where})")

    for missing in package.uncovered_attribution(game_dir, shipped):
        problems.append(
            f"{rel_game}: {missing} is an attribution file not covered by "
            f"`include:` -- the licence must ship with the binary")

    return problems


def main() -> int:
    problems: list[str] = []
    for game_dir in games(REPO_ROOT):
        problems.extend(check_game(game_dir))

    for p in problems:
        print(f"::error::{p}")
        print(f"  {p}", file=sys.stderr)

    if problems:
        print(f"\n{len(problems)} asset problem(s).", file=sys.stderr)
        return 1

    print("Assets: every declared path exists and is covered by its release manifest.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
