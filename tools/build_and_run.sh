#!/bin/bash
# Build a PS2 target in the ps2dev container, run it in PCSX2 and check its log.
#
# Usage: tools/build_and_run.sh [--no-build] [--shot] <dir> [expect]
#   <dir>       directory with a Makefile that produces one .elf
#   expect      text to wait for in PCSX2's log (default: just capture 20s of log)
#   --no-build  skip the container build (the .elf is already built)
#   --shot      after a match, ask PCSX2 for a screenshot (F8)
#
# The container image is built from .devcontainer/Dockerfile, so command-line
# builds and the devcontainer use the same toolchain.

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="wipeout-ps2-dev"
PCSX2_BIN="/Applications/PCSX2.app/Contents/MacOS/PCSX2"
PCSX2_DIR="$HOME/Library/Application Support/PCSX2"
INTERNAL_LOG="$PCSX2_DIR/logs/emuLog.txt"
SNAP_DIR="$PCSX2_DIR/snaps"
WORK_LOG="$ROOT/pcsx2_output.log"
TIMEOUT=20

BUILD=1
SHOT=0
while [ $# -gt 0 ]; do
    case "$1" in
        --no-build) BUILD=0; shift ;;
        --shot) SHOT=1; shift ;;
        *) break ;;
    esac
done

TARGET_DIR="${1:-}"
EXPECT="${2:-}"

if [ -z "$TARGET_DIR" ]; then
    echo "Usage: $0 [--no-build] [--shot] <dir> [expect]"
    exit 1
fi
TARGET_DIR="$(cd "$TARGET_DIR" 2>/dev/null && pwd)" || {
    echo "ERROR: Directory $1 does not exist!"
    exit 1
}

# 1. Build inside the container, with the repo at the same path as on the Mac
if [ "$BUILD" -eq 1 ]; then
    echo "--- Building image $IMAGE ---"
    docker build -q -t "$IMAGE" "$ROOT/.devcontainer" > /dev/null || {
        echo "ERROR: Image build failed!"
        exit 1
    }

    echo "--- Compiling $TARGET_DIR ---"
    docker run --rm -v "$ROOT":"$ROOT" -w "$TARGET_DIR" "$IMAGE" make || {
        echo "ERROR: Compilation failed!"
        exit 1
    }
fi

# 2. Find the resulting ELF file
ELF_PATH=$(find "$TARGET_DIR" -maxdepth 1 -name "*.elf" | head -n 1)
if [ -z "$ELF_PATH" ]; then
    echo "ERROR: No ELF file found in $TARGET_DIR!"
    exit 1
fi
echo "Built: $ELF_PATH"

# 3. Restart PCSX2 with a fresh log, so an old run can't produce a false match
pkill -x PCSX2
for _ in 1 2 3 4 5; do
    pgrep -x PCSX2 > /dev/null || break
    sleep 1
done
rm -f "$INTERNAL_LOG" "$WORK_LOG"
OLD_SNAP_COUNT=$(ls -1 "$SNAP_DIR" 2>/dev/null | wc -l)

echo "--- Launching PCSX2 with $(basename "$ELF_PATH") ---"
"$PCSX2_BIN" "$ELF_PATH" > /dev/null 2>&1 &

# 4. Watch the log
if [ -z "$EXPECT" ]; then
    echo "Capturing log for ${TIMEOUT}s..."
else
    echo "Waiting for \"$EXPECT\" (timeout ${TIMEOUT}s)..."
fi

for _ in $(seq "$TIMEOUT"); do
    sleep 1
    cp "$INTERNAL_LOG" "$WORK_LOG" 2>/dev/null || continue
    if [ -n "$EXPECT" ] && grep -qF "$EXPECT" "$WORK_LOG"; then
        echo "SUCCESS: found \"$EXPECT\""
        grep -F "$EXPECT" "$WORK_LOG" | tail -n 1

        if [ "$SHOT" -eq 1 ]; then
            echo "Requesting screenshot (F8)..."
            osascript -e 'tell application "PCSX2" to activate' \
                      -e 'delay 0.5' \
                      -e 'tell application "System Events" to key code 100' 2>/dev/null
            for _ in 1 2 3 4 5; do
                sleep 1
                if [ "$(ls -1 "$SNAP_DIR" 2>/dev/null | wc -l)" -gt "$OLD_SNAP_COUNT" ]; then
                    echo "Screenshot: $SNAP_DIR/$(ls -t "$SNAP_DIR" | head -n 1)"
                    exit 0
                fi
            done
            echo "INFO: No screenshot appeared. Press F8 in PCSX2 manually."
        fi
        exit 0
    fi
done

if [ -z "$EXPECT" ]; then
    echo "Log captured to $WORK_LOG"
    exit 0
fi
echo "TIMEOUT: \"$EXPECT\" not found. Log: $WORK_LOG"
exit 1
