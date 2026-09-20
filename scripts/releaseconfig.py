"""Reads a scope's .release-config.yml.

One reader. The release workflow, the changelog tool and the packager all need
values out of this file, and each had grown its own parser -- a grep in bash, a
regex in Python, and in the packager's case no parser at all, just the same
names written out a second time. That third one is how three selva releases
tagged and then failed: the copy went stale and nothing compared it to the
original.

Deliberately stdlib-only, like the rest of scripts/. The file uses a small
subset of YAML -- `key: value` scalars and one level of `- item` lists -- so a
real YAML dependency would buy nothing and has to be installed on every runner.
"""

from __future__ import annotations

import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]

CONFIG_NAME = ".release-config.yml"


class ConfigError(Exception):
    """The config is missing, malformed, or missing a required key."""


def _strip_comment(line: str) -> str:
    """Drop a trailing `#` comment.

    Only outside quotes: a repo name never contains one, but a size limit
    written `120  # measured` is normal and must not swallow the value.
    """
    out, quote = [], ""
    for ch in line:
        if quote:
            out.append(ch)
            if ch == quote:
                quote = ""
        elif ch in "\"'":
            quote = ch
            out.append(ch)
        elif ch == "#":
            break
        else:
            out.append(ch)
    return "".join(out)


def parse(text: str) -> dict[str, object]:
    """Parse the supported subset into a dict of str -> str | list[str].

    A key with no value that is followed by indented `- ` lines becomes a list;
    one followed by nothing becomes an empty string, which is how
    `public_repo: ""` and a bare `public_repo:` read the same.
    """
    data: dict[str, object] = {}
    pending: str | None = None

    for raw in text.split("\n"):
        line = _strip_comment(raw).rstrip()
        if not line.strip():
            continue

        stripped = line.strip()
        if stripped.startswith("- "):
            if pending is None:
                raise ConfigError(f"list item outside any key: {stripped!r}")
            if not isinstance(data.get(pending), list):
                data[pending] = []
            data[pending].append(stripped[2:].strip().strip("\"'"))
            continue

        if ":" not in line:
            raise ConfigError(f"not a key: {stripped!r}")

        key, _, value = line.partition(":")
        key = key.strip()
        data[key] = value.strip().strip("\"'")
        # A key holding nothing yet may still grow list items below it, so it
        # stays a string until one actually arrives. That is what lets
        # `public_repo: ""` read as the empty string -- the changelog tool
        # asks this file for a repo NAME and would choke on a list.
        pending = key

    return data


def load(scope_dir: pathlib.Path | str) -> dict[str, object]:
    """Read one scope's config. `scope_dir` is relative to the repo root."""
    path = pathlib.Path(scope_dir)
    if not path.is_absolute():
        path = REPO_ROOT / path
    cfg = path / CONFIG_NAME
    if not cfg.is_file():
        raise ConfigError(f"missing {cfg}")
    return parse(cfg.read_text(encoding="utf-8"))


def get(scope_dir: pathlib.Path | str, key: str, default: object = None) -> object:
    data = load(scope_dir)
    if key not in data:
        if default is not None:
            return default
        raise ConfigError(f"{scope_dir}/{CONFIG_NAME} has no key {key!r}")
    return data[key]


def main() -> int:
    """`releaseconfig.py <scope-dir> <key>` -- so the workflow reads it too.

    Without this the workflow keeps its own grep, and a second parser is the
    thing this module exists to remove.
    """
    if len(sys.argv) != 3:
        print("usage: releaseconfig.py <scope-dir> <key>", file=sys.stderr)
        return 2
    try:
        value = get(sys.argv[1], sys.argv[2], default="")
    except ConfigError as e:
        print(f"::error::{e}", file=sys.stderr)
        return 1
    print("\n".join(value) if isinstance(value, list) else value)
    return 0


if __name__ == "__main__":
    sys.exit(main())
