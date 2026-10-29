#!/bin/bash
# Convert the PS2 Linux kit hardware manuals to searchable text in docs/hw/.
#
# Usage: tools/convert_manuals.sh [manuals_dir]
#   manuals_dir defaults to ~/Development/ps2/manuals
#
# docs/hw/ is gitignored: the manuals are Sony's copyrighted documents and must
# never be committed. Needs pdftotext (brew install poppler).

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$HOME/Development/ps2/manuals}"
OUT="$ROOT/docs/hw"

command -v pdftotext > /dev/null || {
    echo "ERROR: pdftotext not found. Install it with: brew install poppler"
    exit 1
}
[ -d "$SRC" ] || { echo "ERROR: $SRC does not exist"; exit 1; }

mkdir -p "$OUT"

find "$SRC" -type f -iname '*.pdf' | while read -r pdf; do
    name="$(basename "$pdf" | tr '[:upper:]' '[:lower:]')"
    name="${name%.pdf}"
    cp "$pdf" "$OUT/$name.pdf"
    # -layout keeps register tables and bitfield columns mostly aligned.
    pdftotext -layout "$pdf" "$OUT/$name.txt"
    echo "$name: $(grep -c '' "$OUT/$name.txt") lines"
done

echo "Done: $OUT"
