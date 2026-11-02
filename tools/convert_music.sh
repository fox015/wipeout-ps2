#!/bin/bash
# Convert the music in wipeout/music/ from QOA to SPU2 ADPCM (.spu), which
# the PS2 streams from the IOP with no EE work (see tools/spu_music/spu_music.c).
# Run on the Mac; it needs a C compiler (Xcode's command line tools). The .spu
# files go next to the .qoa ones in the gitignored wipeout/ folder.
#
# Usage: tools/convert_music.sh
#
# This re-encodes one lossy format (QOA) into another, so a little quality is
# lost; each file's SNR against its QOA is printed. Encoding from the
# original CD audio instead would avoid that.

set -e
cd "$(dirname "$0")/.."
BIN=tools/spu_music/spu_music
cc -O2 -o "$BIN" tools/spu_music/spu_music.c -lm
for qoa in wipeout/music/*.qoa; do
	"$BIN" "$qoa" "${qoa%.qoa}.spu"
done
