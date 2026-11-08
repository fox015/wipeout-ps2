#!/bin/bash
# GS dump of one benchmark frame: freeze track02's bench lap at frame N, then
# ask PCSX2 for a single-frame GS dump (Shift+F8) and a screenshot (F8).
# A GS dump holds everything the GS was sent, so counts from it don't depend
# on PCSX2's timing (PCSX2 charges almost nothing for GS work).
#
# Usage: tools/gs_dump.sh N [outdir]     (outdir defaults to gsdump/, gitignored)
# Then analyse it with tools/gs_tools.py (see there).

set -u
[ -n "${1:-}" ] || { echo "usage: tools/gs_dump.sh N [outdir]"; exit 1; }
N=$1
cd "$(dirname "$0")/.."
OUT=${2:-gsdump}
mkdir -p "$OUT"
LOG="$HOME/Library/Application Support/PCSX2/logs/emuLog.txt"
SNAP="$HOME/Library/Application Support/PCSX2/snaps"
IMAGE=$(docker build -q .devcontainer) || exit 1
run_make() {
	docker run --rm -v "$PWD":"$PWD" -w "$PWD" "$IMAGE" make "$@" >/dev/null || { echo "gs_dump: build failed"; exit 1; }
}

run_make BENCH=1 BENCH_FREEZE="$N"
tools/build_and_run.sh --no-build . "bench: one lap" >/dev/null 2>&1 || { echo "gs_dump: didn't start"; run_make; exit 1; }
for ((t = 0; t < 300; t += 2)); do
	grep -q "bench: frozen" "$LOG" && break
	sleep 2
done
grep -q "bench: frozen" "$LOG" || { echo "gs_dump: didn't reach frame $N"; run_make; exit 1; }
sleep 2

MARK=$(mktemp)
osascript -e 'tell application "PCSX2" to activate' \
	-e 'tell application "System Events" to tell process "PCSX2" to perform action "AXRaise" of (first window whose name does not start with "Log Window")' \
	-e 'delay 0.6' \
	-e 'tell application "System Events" to key code 100 using shift down' > /dev/null
dump=""
for ((t = 0; t < 90; t++)); do
	dump=$(find "$SNAP" -newer "$MARK" -name "*.gs.zst" | head -n 1)
	[ -n "$dump" ] && break
	sleep 1
done
rm -f "$MARK"
[ -n "$dump" ] || { echo "gs_dump: no GS dump appeared"; run_make; exit 1; }
sleep 3 # let PCSX2 finish writing it
cp "$dump" "$OUT/frame$N.gs.zst"
shot=$(tools/pcsx2_input.sh shot) && sleep 2 && cp "$shot" "$OUT/frame$N.png"
echo "$OUT/frame$N.gs.zst"
[ -f "$OUT/frame$N.png" ] && echo "$OUT/frame$N.png"

run_make
