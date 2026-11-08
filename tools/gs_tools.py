#!/usr/bin/env python3
"""
Run ps-probe's GS dump tools on a wipeout-ps2 dump from tools/gs_dump.sh,
with our settings (tools/psprobe.toml).

    tools/gs_tools.py stats  gsdump/frame1200.gs.zst
    tools/gs_tools.py tris   gsdump/frame1200.gs.zst
    tools/gs_tools.py zsim   gsdump/frame1200.gs.zst
    tools/gs_tools.py clut   gsdump/frame1200.gs.zst   (also flush, uploads, fill, passes, cld4)
    tools/gs_tools.py binds  A.gs.zst B.gs.zst

ps-probe is the user's local toolset, outside this repo: $PSPROBE (default
~/Development/ps-probe), run with its own .venv (Python 3.14 for zstd, numpy
and Pillow for zsim). Options after the dump override the profile, e.g.
--vsyncs 2. zsim doesn't model the alpha test, and draws with ZMSK=1
(transparent) never own a pixel, so its "hidden" figures are approximate.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.environ.get("PSPROBE", os.path.expanduser("~/Development/ps-probe"))
PYTHON = os.path.join(ROOT, ".venv", "bin", "python")
OLD = {"gsdump": "stats", "triclass": "tris", "cld4sim": "cld4", "gsab": None}

args = sys.argv[1:]
if not args or args[0] in ("-h", "--help"):
    sys.exit(__doc__)
if args[0] in OLD:                 # the names of the original Psychonauts scripts
    args = args[1:] if OLD[args[0]] is None else [OLD[args[0]]] + args[1:]
if not os.path.exists(PYTHON):
    sys.exit(f"gs_tools: no ps-probe at {ROOT} (set $PSPROBE; see its README)")
os.execv(PYTHON, [PYTHON, "-m", "psprobe", "ps2", "gs", *args,
                  "--profile", os.path.join(HERE, "psprobe.toml")])
