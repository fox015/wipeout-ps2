#!/bin/bash
# Drive a running PCSX2 from the Mac: press pad buttons and take screenshots.
# Needs Accessibility permission for the app running this script.
#
# Usage:
#   tools/pcsx2_input.sh hold <key> [seconds]   hold a key bound to the pad
#   tools/pcsx2_input.sh shot                    screenshot (F8); prints the PNG path
#
# Keys follow PCSX2's Pad1 keyboard bindings: k = Cross, l = Circle,
# j = Square, i = Triangle, q = L1, e = R1, 1 = L2, 3 = R2, w/a/s/d = left stick.
# A key must be held across at least one frame for the game to see it, and
# System Events can only hold character keys, so press Cross rather than Start.

SNAP_DIR="$HOME/Library/Application Support/PCSX2/snaps"

# Bring PCSX2 to the front with its game window focused: keys sent while its
# Log Window has focus never reach the pad.
FOCUS_GAME=(-e 'tell application "PCSX2" to activate'
            -e 'tell application "System Events" to tell process "PCSX2" to perform action "AXRaise" of (first window whose name does not start with "Log Window")'
            -e 'delay 0.2')

case "${1:-}" in
    hold)
        [ -n "${2:-}" ] || { echo "Usage: $0 hold <key> [seconds]"; exit 1; }
        osascript "${FOCUS_GAME[@]}" \
                  -e "tell application \"System Events\" to key down \"$2\"" \
                  -e "delay ${3:-0.25}" \
                  -e "tell application \"System Events\" to key up \"$2\""
        ;;
    shot)
        before=$(ls -1 "$SNAP_DIR" 2>/dev/null | wc -l)
        osascript "${FOCUS_GAME[@]}" -e 'delay 0.6' \
                  -e 'tell application "System Events" to key code 100' > /dev/null
        for _ in 1 2 3 4 5 6 7 8 9 10; do
            sleep 1
            if [ "$(ls -1 "$SNAP_DIR" 2>/dev/null | wc -l)" -gt "$before" ]; then
                echo "$SNAP_DIR/$(ls -t "$SNAP_DIR" | head -n 1)"
                exit 0
            fi
        done
        echo "ERROR: no screenshot appeared"
        exit 1
        ;;
    *)
        echo "Usage: $0 hold <key> [seconds] | shot"
        exit 1
        ;;
esac
