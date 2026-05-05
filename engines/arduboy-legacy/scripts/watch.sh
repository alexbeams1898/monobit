#!/usr/bin/env bash
# Poll the source tree; on any change, rebuild and relaunch Ardens.
# Plain bash so it works in MSYS / Git Bash with no extra deps.

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

ARDENS="/c/Users/alexb/Tools/Ardens/Ardens.exe"
HEX="build/rpg-arduboy-release/rpg.hex"

# Hash of the modtimes of every watched source file. When this changes,
# something was edited.
fingerprint() {
  find engine platform games Makefile -type f \
       \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.c' -o -name 'Makefile' \) \
       -printf '%T@ %p\n' 2>/dev/null | sort | sha1sum | cut -d' ' -f1
}

build_and_run() {
  echo
  echo "==> Building..."
  if make all; then
    echo "==> Launching Ardens..."
    # Ardens uses `file=` CLI syntax (per its README), not positional args.
    # cygpath -m yields forward-slash paths with drive letter (C:/foo/bar)
    # so the shell doesn't eat backslashes as escape sequences.
    "$ARDENS" "file=$(cygpath -m "$ROOT/$HEX")" &
  else
    echo "==> Build failed; not launching."
  fi
}

last=""
echo "watching source files... (Ctrl-C to stop)"
build_and_run
last="$(fingerprint)"

while true; do
  sleep 1
  curr="$(fingerprint)"
  if [ "$curr" != "$last" ]; then
    last="$curr"
    build_and_run
  fi
done
