#!/bin/bash
# Benchmark the track02 race intro, whose camera is scripted, so runs compare.
# Starts the built ELF in PCSX2, presses Cross from the title until the race
# loads, then averages the prof: lines of seconds 2-4 of the intro.
#
# Usage: tools/bench_intro.sh     (build first; this doesn't)
# Prints: fps, ms/frame of work and render, uncapped fps, frames over budget.
# PCSX2 times are its own estimates of EE cycles: compare runs, don't treat
# them as hardware figures.

cd "$(dirname "$0")/.."
LOG="$HOME/Library/Application Support/PCSX2/logs/emuLog.txt"

tools/build_and_run.sh --no-build . "music: host" >/dev/null 2>&1
sleep 4
for try in 1 2 3 4 5 6 7 8; do
	grep -q "load: wipeout/track02/sky.prm" "$LOG" && break
	tools/pcsx2_input.sh hold k 0.2
	sleep 2.5
done
sleep 7

START=$(grep -n "load: wipeout/track02/sky.prm" "$LOG" | head -1 | cut -d: -f1)
[ -n "$START" ] || { echo "bench_intro: the race didn't start"; exit 1; }

tail -n +"$START" "$LOG" | grep "prof:" | sed -n 2,4p | awk '
{
	for (i = 1; i <= NF; i++) {
		if ($i == "frames,") f = $(i-1)
		if ($i == "work") w = $(i+1)
		if ($i == "render") r = $(i+1)
		if ($i == "polys,") p = $(i-1)
		if ($i == "uncapped") u = $(i+1)
		if ($i == "over") o = $(i-2)
	}
	n++; F += f; W += w; R += r; P += p; U += u; O += o
}
END {
	if (!n) { print "bench_intro: no prof: lines"; exit 1 }
	printf "intro: %.0f fps, work %.2f ms, render %.2f ms, %.0f polys, %.2f us/poly, uncapped %.0f fps, %.1f frames/s over budget\n",
		F/n, W/n, R/n, P/n, R/P*1000, U/n, O/n
}'
