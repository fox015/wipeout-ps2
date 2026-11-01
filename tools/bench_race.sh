#!/bin/bash
# Benchmark 45 seconds of a race on track02, holding thrust (the ship's
# line and the other ships vary between runs, so expect some noise).
# Starts the built ELF in PCSX2 and presses Cross from the title to start.
#
# Usage: tools/bench_race.sh     (build first; this doesn't)
# Prints: render cost per polygon, seconds at 60fps, the heaviest views
# (over 3000 polygons), and frames over budget. PCSX2 times are its own
# estimates of EE cycles: compare runs, don't treat them as hardware figures.

cd "$(dirname "$0")/.."
LOG="$HOME/Library/Application Support/PCSX2/logs/emuLog.txt"

tools/build_and_run.sh --no-build . "music: host" >/dev/null 2>&1
sleep 3
for try in 1 2 3 4 5 6 7 8; do
	grep -q "load: wipeout/track02/sky.prm" "$LOG" && break
	tools/pcsx2_input.sh hold k 0.2
	sleep 2
done
grep -q "load: wipeout/track02/sky.prm" "$LOG" || { echo "bench_race: the race didn't start"; exit 1; }
sleep 6
tools/pcsx2_input.sh hold k 45

START=$(grep -n "load: wipeout/track02/sky.prm" "$LOG" | head -1 | cut -d: -f1)
tail -n +"$START" "$LOG" | grep "prof:" | tail -45 | awk '
{
	for (i = 1; i <= NF; i++) {
		if ($i == "frames,") f = $(i-1)
		if ($i == "render") r = $(i+1)
		if ($i == "xform") x = $(i+1)
		if ($i == "polys,") p = $(i-1)
		if ($i == "over") o = $(i-2)
	}
	if (p == 0) next
	n++; us += (r - x) * 1000 / p; O += o
	if (f >= 60) ok++
	if (p > 3000) { h++; hr += r }
}
END {
	if (!n) { print "bench_race: no prof: lines"; exit 1 }
	printf "race: draw %.2f us/poly; %d of %d s at 60fps; heavy views (>3000 polys) render %.2f ms over %d s; %d frames over budget\n",
		us/n, ok, n, (h ? hr/h : 0), h, O
}'
