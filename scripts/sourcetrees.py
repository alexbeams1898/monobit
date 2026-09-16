"""What the source linters look at.

One definition, imported by all of them. Each linter used to carry its own
hand-written list of directories, and each list stopped covering a game the day
one was added -- silently, since a linter that scans nothing still passes.
"""

from __future__ import annotations

import pathlib

# Every directory under these is a tree we own.
TREE_PARENTS = ("engines", "games")

# Imported from another repo and held to its rules. Read-only reference.
ARCHIVED = ("arduboy-legacy", "rpg-arduboy")

# Generated or third-party, wherever it appears in a path.
EXCLUDES = ("build", "_deps", "third_party", "vendor", ".git")

CPP_EXTENSIONS = (".cpp", ".cc", ".cxx")
HEADER_EXTENSIONS = (".h", ".hpp")
SOURCE_EXTENSIONS = CPP_EXTENSIONS + HEADER_EXTENSIONS


def repo_root() -> pathlib.Path:
    """The directory holding scripts/, whoever the caller is."""
    return pathlib.Path(__file__).resolve().parents[1]


def source_trees(root: pathlib.Path | None = None) -> list[pathlib.Path]:
    root = root or repo_root()
    trees = []
    for parent in TREE_PARENTS:
        base = root / parent
        if not base.is_dir():
            continue
        for child in sorted(base.iterdir()):
            if child.is_dir() and child.name not in ARCHIVED:
                trees.append(child)
    return trees


def excluded(path: pathlib.Path) -> bool:
    return any(part in EXCLUDES for part in path.parts)


def source_files(root: pathlib.Path | None = None,
                 extensions: tuple[str, ...] = SOURCE_EXTENSIONS) -> list[pathlib.Path]:
    root = root or repo_root()
    out = []
    for tree in source_trees(root):
        for p in sorted(tree.rglob("*")):
            if p.is_file() and p.suffix in extensions and not excluded(p):
                out.append(p)
    return out


def comment_lines(path: pathlib.Path):
    """Yield (line_number, text) for lines that are comments.

    Block comments count from their opening line to their close. A `//` inside
    a string literal is not a comment, but treating one as such only risks a
    linter reading a line it should have skipped -- cheap next to the parser
    that telling them apart properly would need.
    """
    in_block = False
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return
    for n, raw in enumerate(text.split("\n"), start=1):
        s = raw.strip()
        if in_block:
            yield n, s
            if "*/" in s:
                in_block = False
            continue
        if s.startswith("/*"):
            yield n, s
            if "*/" not in s:
                in_block = True
            continue
        if s.startswith("//"):
            yield n, s


def comment_blocks(path: pathlib.Path):
    """Yield contiguous runs of comment lines as [(line_number, text), ...].

    Carve-outs read better against a block than a line: a version label three
    lines into a paragraph about save migration is versioning data, and the
    word "migration" may well sit in the sentence above it.
    """
    block: list = []
    last = -2
    for n, text in comment_lines(path):
        if n != last + 1 and block:
            yield block
            block = []
        block.append((n, text))
        last = n
    if block:
        yield block
