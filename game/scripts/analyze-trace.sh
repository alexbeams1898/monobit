#!/usr/bin/env bash
# analyze-trace.sh — extract and summarize a Tracy trace file.
#
# Usage:
#   ./scripts/analyze-trace.sh [trace-file]
#
# If no file is given, picks the most recently modified .tracy in traces/.

set -e

CSVEXPORT="tools/tracy/tracy-csvexport.exe"
TRACES_DIR="traces"
mkdir -p "$TRACES_DIR"

if [ ! -f "$CSVEXPORT" ]; then
    echo "ERROR: $CSVEXPORT not found. Expected at repo root." >&2
    exit 1
fi

# Pick trace file — argument or most recent in traces/
if [ -n "$1" ]; then
    TRACE="$1"
else
    TRACE=$(ls -t "$TRACES_DIR"/*.tracy 2>/dev/null | head -1)
    if [ -z "$TRACE" ]; then
        echo "No .tracy files found in $TRACES_DIR/. Pass a file path as argument." >&2
        exit 1
    fi
fi

echo "=== Trace: $TRACE ==="
echo ""

# --- Summary stats ---
echo "--- Zone summary (avg/min/max ms) ---"
"$CSVEXPORT" "$TRACE" | awk -F',' 'NR>1 {
    printf "%-20s  avg=%6.2f  min=%6.2f  max=%7.2f  count=%d\n",
        $1,
        $7/1e6, $8/1e6, $9/1e6, $6
}'

echo ""

# --- Top 10 slowest frames (render only) ---
echo "--- Top 10 slowest render frames ---"
"$CSVEXPORT" -u "$TRACE" | awk -F',' '
NR>1 && $1=="render" {print $5, $4}
' | sort -rn | head -10 | awk '{
    printf "  t=%7.3fs  duration=%7.2fms\n", $2/1e9, $1/1e6
}'

echo ""

# --- Messages (gameplay events) ---
MSG=$("$CSVEXPORT" -m "$TRACE" 2>&1)
if echo "$MSG" | grep -q "no messages"; then
    echo "--- No messages (build with TRACY_ENABLE=ON and re-record) ---"
else
    echo "--- Gameplay event messages ---"
    echo "$MSG"
fi
