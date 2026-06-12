#!/usr/bin/env bash
# Smoke test: launches selva-oscura.exe, waits for the main-loop-ready
# marker in the log, then kills the process. Fails if the marker
# doesn't appear within MAX_WAIT seconds.
#
# Catches every pre-main / early-boot crash: static-init-order fiasco,
# missing asset that aborts boot, shader compile failure, etc. The
# game's startup output IS the evidence; we just need to see it.
#
# Why this exists: the WIN32-subsystem build has no console, so a
# pre-main segfault produces zero output and exit 139 with no
# diagnostic. Tests don't exercise main.cpp's boot path. This script
# is the only thing that runs the actual exe in CI.
#
# Usage: bash games/selva-oscura/scripts/smoke_test.sh [path/to/exe]
# Default exe path: build/bin/selva-oscura/selva-oscura.exe

set -u  # NOT -e -- we want explicit exit codes with messages

readonly MAX_WAIT_SECONDS=20
readonly MARKER='[smoke] main loop ready'

EXE_PATH="${1:-}"

if [[ -z "$EXE_PATH" ]]; then
    # Auto-detect: Windows ships .exe; Linux/macOS strip the suffix.
    for candidate in \
        build/bin/selva-oscura/selva-oscura.exe \
        build/bin/selva-oscura/selva-oscura \
        build/bin/selva-oscura; do
        if [[ -x "$candidate" ]]; then
            EXE_PATH="$candidate"
            break
        fi
    done
fi

if [[ -z "$EXE_PATH" || ! -x "$EXE_PATH" ]]; then
    echo "[smoke] FAIL: exe not found or not executable: ${EXE_PATH:-<none>}" >&2
    exit 2
fi

EXE_DIR="$(dirname "$EXE_PATH")"
EXE_NAME="$(basename "$EXE_PATH")"
LOG_PATH="$EXE_DIR/selva-oscura.log"

rm -f "$LOG_PATH"

echo "[smoke] launching $EXE_PATH"
(cd "$EXE_DIR" && "./$EXE_NAME") &
PID=$!

# Poll the log for the marker. Up to MAX_WAIT_SECONDS, checking every 200ms.
FOUND=0
for _ in $(seq 1 $((MAX_WAIT_SECONDS * 5))); do
    if [[ -f "$LOG_PATH" ]] && grep -qF "$MARKER" "$LOG_PATH"; then
        FOUND=1
        break
    fi
    if ! kill -0 "$PID" 2>/dev/null; then
        # Process exited before marker. Almost certainly a boot crash.
        echo "[smoke] FAIL: exe exited before main-loop-ready marker"
        echo "---log tail---"
        tail -50 "$LOG_PATH" 2>&1 || echo "(no log file written)"
        exit 1
    fi
    sleep 0.2
done

# Kill the still-running process either way (we got what we needed).
kill -TERM "$PID" 2>/dev/null
# Windows kill -TERM may not actually terminate; force after a beat.
sleep 0.5
kill -KILL "$PID" 2>/dev/null
wait "$PID" 2>/dev/null || true

if [[ $FOUND -ne 1 ]]; then
    echo "[smoke] FAIL: marker '$MARKER' did not appear within ${MAX_WAIT_SECONDS}s"
    echo "---log tail---"
    tail -50 "$LOG_PATH" 2>&1 || echo "(no log file written)"
    exit 1
fi

echo "[smoke] OK: main loop reached"
exit 0
