#!/usr/bin/env bash
# analyze-trace.sh — extract and summarize a Tracy trace file for selva-oscura.
#
# Usage (run from repo root):
#   ./games/selva-oscura/scripts/analyze-trace.sh [trace-file]
#
# If no file is given, picks the most recently modified .tracy in
# games/selva-oscura/.traces/.

set -e

# Resolve repo root via this script's location, so we can be invoked from
# anywhere. The script lives at games/selva-oscura/scripts/analyze-trace.sh.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." &> /dev/null && pwd)"
CSVEXPORT="$REPO_ROOT/tools/tracy/tracy-csvexport.exe"
TRACES_DIR="$REPO_ROOT/games/selva-oscura/.traces"
mkdir -p "$TRACES_DIR"

if [ ! -f "$CSVEXPORT" ]; then
    echo "ERROR: $CSVEXPORT not found." >&2
    exit 1
fi

# Pick trace file — argument or most recent in .traces/
if [ -n "$1" ]; then
    TRACE="$1"
    # Auto-resolve short names: "1" -> ".traces/1.tracy"
    if [ ! -f "$TRACE" ]; then
        [ "${TRACE%.tracy}" = "$TRACE" ] && TRACE="${TRACE}.tracy"
        [ ! -f "$TRACE" ] && TRACE="$TRACES_DIR/$TRACE"
    fi
else
    TRACE=$(ls -t "$TRACES_DIR"/*.tracy 2>/dev/null | head -1)
    if [ -z "$TRACE" ]; then
        echo "No .tracy files found in $TRACES_DIR/. Pass a file path as argument." >&2
        exit 1
    fi
fi

if [ ! -f "$TRACE" ]; then
    echo "ERROR: File not found: $TRACE" >&2
    echo "  Try: $0 $TRACES_DIR/yourfile.tracy" >&2
    exit 1
fi

echo "=== Trace: $TRACE ==="
echo ""

# --- Summary stats ---
echo "--- Zone summary (avg/min/max ms) ---"
"$CSVEXPORT" "$TRACE" | awk -F',' 'NR>1 {
    printf "%-30s  avg=%6.2f  min=%6.2f  max=%7.2f  count=%d\n",
        $1,
        $7/1e6, $8/1e6, $9/1e6, $6
}'

echo ""

# --- Top 10 slowest zones (unmerged, with 30s timeout) ---
echo "--- Top 10 slowest individual zone calls ---"
TMPFILE=$(mktemp)
if timeout 30 "$CSVEXPORT" -u "$TRACE" > "$TMPFILE" 2>/dev/null; then
    awk -F',' 'NR>1 { printf "%-30s  t=%7.3fs  duration=%7.2fms\n", $1, $4/1e9, $5/1e6 }' \
        "$TMPFILE" | sort -t= -k3 -rn | head -10
else
    echo "  (skipped -- unmerged export timed out after 30s; trace too large)"
fi
rm -f "$TMPFILE"

echo ""

# --- Messages (gameplay events) ---
MSG=$("$CSVEXPORT" -m "$TRACE" 2>&1)
if echo "$MSG" | grep -q "no messages"; then
    echo "--- No messages emitted by game (would need TracyMessage calls in code) ---"
else
    echo "--- Gameplay event messages ---"
    echo "$MSG"
fi
