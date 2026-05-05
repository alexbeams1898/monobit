"""Run the efficiency linter as a module:

    python -m efficiency_lint              # check all rules, fail on any error
    python -m efficiency_lint --list       # list registered rules + descriptions
    python -m efficiency_lint --rules X,Y  # run only the named rules
    python -m efficiency_lint --warn-only  # report findings but exit 0

Wire into the build via Makefile (see `make audit` target).
"""

from __future__ import annotations

import argparse
import sys

from .lint import iter_sources
from .rules import ALL_RULES


def main() -> int:
    ap = argparse.ArgumentParser(description="1-bit Arduboy efficiency linter")
    ap.add_argument("--list", action="store_true",
                    help="list available rules and exit")
    ap.add_argument("--rules", default="",
                    help="comma-separated rule names to run (default: all)")
    ap.add_argument("--warn-only", action="store_true",
                    help="report findings but exit 0 (informational mode)")
    args = ap.parse_args()

    if args.list:
        print("Available rules:")
        for name, _, doc in ALL_RULES:
            print(f"  {name:<32}  {doc}")
        return 0

    selected = set(args.rules.split(",")) if args.rules else None
    rules = [(n, fn) for (n, fn, _) in ALL_RULES
             if not selected or n in selected]

    findings = []
    for relpath, text in iter_sources():
        for _, fn in rules:
            findings.extend(fn(relpath, text))

    if not findings:
        print(f"efficiency-lint OK ({len(rules)} rule(s), 0 findings)")
        return 0

    # Group by rule for readability.
    findings.sort(key=lambda f: (f.rule, f.path, f.line))
    sys.stderr.write("\n#### EFFICIENCY-LINT FINDINGS ####\n")
    for f in findings:
        sys.stderr.write(f"  {f.render()}\n")
    sys.stderr.write(f"\n  ({len(findings)} finding(s) across {len(rules)} rule(s))\n\n")

    if args.warn_only:
        return 0
    # Only fail on errors. Warnings are reported but don't break the build —
    # they exist for findings worth surfacing without forcing immediate fix.
    if any(f.severity == "error" for f in findings):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
