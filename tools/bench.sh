#!/bin/bash
# Benchmark: build a BENCH ELF, let it race by itself in PCSX2, print its
# results, then rebuild the normal ELF. No pad input is sent, so PCSX2 can be
# left alone (or in the background) while it runs.
#
# Usage: tools/bench.sh [all]
#   (none)  one lap of track02, about 2 minutes
#   all     one lap of every circuit, about 15 minutes
#
# Prints the game's bench: lines: per track, the intro and the first lap
# (frames, work and render ms per frame, polygons, KB sent to the GS, uncapped
# fps, frames over budget) and the lap's slowest track sections. The race
# plays out the same on every run (see src/bench_ps2.h), so frame counts
# should match between runs.
# In PCSX2 the times are the emulator's estimates of EE cycles: compare runs,
# don't quote them as hardware figures.

cd "$(dirname "$0")/.."
MODE=1
[ "${1:-}" = "all" ] && MODE=all
LOG="$HOME/Library/Application Support/PCSX2/logs/emuLog.txt"
IMAGE=$(docker build -q .devcontainer) || exit 1
run_make() {
	docker run --rm -v "$PWD":"$PWD" -w "$PWD" "$IMAGE" make "$@" >/dev/null || { echo "bench: build failed"; exit 1; }
}

run_make BENCH=$MODE
tools/build_and_run.sh --no-build . "bench: one lap" >/dev/null 2>&1 || { echo "bench: didn't start"; exit 1; }
LIMIT=$([ $MODE = all ] && echo 1800 || echo 300)
for ((t = 0; t < LIMIT; t += 5)); do
	grep -q "bench: done" "$LOG" && break
	sleep 5
done
grep "bench:" "$LOG" | sed 's/^\[[^]]*\] //' | uniq
grep -q "bench: done" "$LOG" || echo "bench: no result after ${LIMIT}s"

# Leave the normal build in place.
run_make
