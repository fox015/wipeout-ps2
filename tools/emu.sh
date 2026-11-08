# Which emulator the tools drive. Sourced by build_and_run.sh, bench.sh,
# gs_dump.sh and pcsx2_input.sh.
#
# Default: ARMSX2, the native ARM64 fork of PCSX2. PCSX2 for macOS is an Intel
# build running under Rosetta (about 70% of real time on this Mac). Both keep
# the same settings layout, BIOS, HostFs and Pad1 keyboard bindings.
# EMU=pcsx2 picks PCSX2 instead, e.g. to check a surprising result.
# Timing estimates differ between the two: compare runs on the same one.

EMU="${EMU:-armsx2}"
case "$EMU" in
	armsx2)
		EMU_APP="ARMSX2"
		EMU_DIR="$HOME/Library/Application Support/ARMSX2"
		EMU_LOG="$EMU_DIR/logs/emulog.txt"
		;;
	pcsx2)
		EMU_APP="PCSX2"
		EMU_DIR="$HOME/Library/Application Support/PCSX2"
		EMU_LOG="$EMU_DIR/logs/emuLog.txt"
		;;
	*)
		echo "EMU must be armsx2 or pcsx2" >&2
		exit 1
		;;
esac
EMU_BIN="/Applications/$EMU_APP.app/Contents/MacOS/$EMU_APP"
SNAP_DIR="$EMU_DIR/snaps"

# Bring the emulator to the front with its game window focused: keys sent
# while its Log Window has focus never reach the pad.
EMU_FOCUS=(-e "tell application \"$EMU_APP\" to activate"
           -e "tell application \"System Events\" to tell process \"$EMU_APP\" to perform action \"AXRaise\" of (first window whose name does not start with \"Log Window\")"
           -e 'delay 0.2')
