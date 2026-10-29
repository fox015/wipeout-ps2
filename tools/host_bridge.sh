#!/bin/bash
# Run on the Mac. Lets a build inside the devcontainer start PCSX2 on the host.
#
# Inside the container, after building:
#   echo "tools/smoke" > run_on_host.trigger
# This script sees the file, deletes it, and runs build_and_run.sh --no-build
# on that directory. An optional second line is passed on as the expect string.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TRIGGER_FILE="$ROOT/run_on_host.trigger"

echo "--- Host bridge started (watching $TRIGGER_FILE) ---"

while true; do
    if [ -f "$TRIGGER_FILE" ]; then
        TARGET_DIR=$(sed -n 1p "$TRIGGER_FILE")
        EXPECT=$(sed -n 2p "$TRIGGER_FILE")
        rm -f "$TRIGGER_FILE"

        echo "[$(date +'%H:%M:%S')] Running: $TARGET_DIR"
        (cd "$ROOT" && "$ROOT/tools/build_and_run.sh" --no-build "$TARGET_DIR" "$EXPECT")
        echo "[$(date +'%H:%M:%S')] Done: $TARGET_DIR"
        echo "--------------------------------------------------------"
    fi
    sleep 0.5
done
