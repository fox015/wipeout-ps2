#!/usr/bin/env python3
"""
Run the GS-dump analysis tools from the user's Psychonauts PS2 investigation
(not in this repo) on a wipeout-ps2 dump from tools/gs_dump.sh.

    python3 tools/gs_tools.py gsdump   gsdump/frame1200.gs.zst
    python3 tools/gs_tools.py triclass gsdump/frame1200.gs.zst
    python3 tools/gs_tools.py zsim     gsdump/frame1200.gs.zst --fbp 0
    python3 tools/gs_tools.py gsab clut gsdump/frame1200.gs.zst

The tools live in $PSYCHONAUTS_TOOLS (default ~/scratch/psychonauts/tools)
and need Python 3.14 (stdlib zstd), plus numpy and Pillow for zsim; that
project's .venv has them. They assume Psychonauts' setup, so this adapts them:
- gsKit sets XYOFFSET (2048,2048) and SCISSOR (640x480) once at boot, so they
  aren't in a dump: the parser starts with ours.
- zsim's target is 640x480, and fans (one PRIM per wipeout polygon) are
  labelled by winding as strips are, so its per-batch figures are per polygon.
- VSYNCS=1 in the environment limits a run to the dump's first vsync. Use it:
  a frozen bench frame repeats, and zsim would count the repeat as the first
  copy's overdraw. That vsync draws to FBP 0.
zsim doesn't model the alpha test, and draws with ZMSK=1 (transparent) never
own a pixel, so its "hidden" figures are approximate.
"""
import os
import runpy
import sys

TOOLS = os.environ.get("PSYCHONAUTS_TOOLS", os.path.expanduser("~/scratch/psychonauts/tools"))
sys.path.insert(0, TOOLS)
import gsdump  # noqa: E402

_init = gsdump.GifParser.__init__


def _wipeout_init(self, *a, **k):
    _init(self, *a, **k)
    self.offset = [(2048 * 16, 2048 * 16)] * 2
    self.scissor = [(0, 639, 0, 479)] * 2


gsdump.GifParser.__init__ = _wipeout_init

if os.environ.get("VSYNCS"):
    _packets = gsdump.packets
    gsdump.packets = lambda d, max_vsyncs=None: _packets(d, int(os.environ["VSYNCS"]))


def _zsim():
    import zsim

    zsim.Sim.__init__.__defaults__ = (640, 480)

    def on_tri(self, w, zs, sa, t, ctx, tgt):
        if not self._target(ctx) or sa == 0:
            return
        label = (1 if sa > 0 else 2) if t in (4, 5) else 3
        if t in (4, 5):
            self.bstat[self.bid][zsim.LABELS[label] + " tris"] += 1
        ox, oy = self.offset[ctx]
        self._draw(ctx, [(x * 16 + ox, y * 16 + oy) for x, y in w], zs, label)

    zsim.Sim.on_tri = on_tri
    zsim.main()


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    tool = sys.argv.pop(1)
    sys.argv[0] = tool
    if tool == "gsdump":
        gsdump.main()
    elif tool == "zsim":
        _zsim()
    elif tool in ("triclass", "gsab", "cld4sim"):
        # Run as a script; it imports the patched gsdump from sys.modules.
        runpy.run_path(os.path.join(TOOLS, tool + ".py"), run_name="__main__")
    else:
        sys.exit(f"unknown tool {tool}")


if __name__ == "__main__":
    main()
