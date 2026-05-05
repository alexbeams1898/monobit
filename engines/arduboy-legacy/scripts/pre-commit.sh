#!/usr/bin/env bash
# Pre-commit hook. Two checks, both blocking:
#
# 1. clang-format clean for staged C/C++ files.
# 2. Platform parity: BOTH Arduboy and SDL builds compile, when staged
#    files include any code that ships to either platform (anything
#    under engine/, games/, or platform/, plus the Makefile and
#    CMakeLists.txt). Pure-docs / pure-tools changes skip the build.
#
# Per docs/platform-parity.md, the player-facing behavior must be
# identical across platforms. This hook only enforces the *compile*
# half — running both targets and visually verifying behavior matches
# is still the dev's job (`make ardens` + `make sdl-run`). What this
# catches is the silent-rot case: a commit that compiles for Arduboy
# but breaks SDL (or vice versa) because someone forgot to provide a
# matching platform implementation.
#
# Run from the repo root.
#
# Install with: make setup-hooks

set -u

# Find staged files (added/modified/copied/renamed). Avoids deleted.
mapfile -t all_staged < <(git diff --cached --name-only --diff-filter=ACMR)

if [ ${#all_staged[@]} -eq 0 ]; then
  exit 0
fi

# ---- Check 1: clang-format -----------------------------------------------
mapfile -t cpp_files < <(printf '%s\n' "${all_staged[@]}" | grep -E '\.(c|cc|cpp|h|hpp)$' || true)

if [ ${#cpp_files[@]} -gt 0 ]; then
  bad=()
  for f in "${cpp_files[@]}"; do
    # --dry-run + -Werror: clang-format exits non-zero if it would change anything.
    if ! clang-format --dry-run --Werror "$f" >/dev/null 2>&1; then
      bad+=("$f")
    fi
  done

  if [ ${#bad[@]} -gt 0 ]; then
    echo "✗ The following files are not clang-format clean:"
    for f in "${bad[@]}"; do
      echo "    $f"
    done
    echo ""
    echo "Run:  clang-format -i ${bad[*]}"
    echo "      git add ${bad[*]}"
    echo "and commit again."
    exit 1
  fi
fi

# ---- Check 2: platform parity (both builds compile) ----------------------
# Trigger only when staged changes touch code that ships to either
# platform. Files purely under docs/, tools/, scripts/, or art/ don't
# affect platform binaries; skip the (~30 s warm) full build for them.
parity_relevant=$(printf '%s\n' "${all_staged[@]}" | grep -E '^(engine/|games/|platform/|Makefile$|CMakeLists\.txt$)' || true)

if [ -n "$parity_relevant" ]; then
  echo "→ Platform parity check (both targets must compile)…"
  if ! make -s verify-parity >/tmp/parity.log 2>&1; then
    echo ""
    echo "✗ Platform parity check FAILED. One or both targets did not"
    echo "  compile cleanly. Tail of build log:"
    echo ""
    tail -20 /tmp/parity.log | sed 's/^/    /'
    echo ""
    echo "  Full log: /tmp/parity.log"
    echo ""
    echo "  Per docs/platform-parity.md, every change that ships code to"
    echo "  Arduboy must also ship matching code to SDL (and vice versa)."
    echo "  Fix the broken target and commit again."
    exit 1
  fi
  echo "  ✓ Both targets compile."
fi

exit 0
