# wipeout-ps2 roadmap

How the Dreamcast port becomes a native PS2 port: the stages, what "done" means
for each, and the architecture decisions behind them.

Each stage lands as one PR, with one commit per step inside it (see "How changes
land" in `CLAUDE.md`). Finished stages are tagged `stage-N`.

## Targets
- **60fps, locked to vsync**, on real hardware. (Higher isn't meaningful: the PS2
  outputs at 60Hz NTSC/480p/VGA or 50Hz PAL.)
- **Real hardware comes at stage 9.** Until the console is set up, each stage is
  checked in PCSX2 only. PCSX2 hides DMA, timing and alignment bugs, so stage 9
  starts with a checklist of what only hardware can confirm (see stage 9).

## Stages

| # | Stage | Done when |
|---|---|---|
| 0 | Build tooling ✅ | A placeholder `main()` builds in the container and prints to PCSX2's log, through `build_and_run.sh` |
| 1 | Compiles and links ✅ | `wipeout.elf` links, with every PS2 layer stubbed |
| 2 | Boots headless ✅ | Logs show assets loading from `host:` and the frame counter rising, with no crash |
| 3 | 2D on screen ✅ | Title screen and menus visible (untextured) |
| 4 | Pad input ✅ | Can navigate menus and start a race |
| 5 | 3D untextured ✅ | Track and ships visible as flat/Gouraud polygons, correctly clipped and depth-tested |
| 6 | Textures and blending ✅ | The game looks right |
| 7 | Sound | Sound effects and music in menus and races |
| 8 | Saves | Settings and best times survive a power cycle on a memory card |
| 9 | Real hardware and packaging | Runs on a real PS2, with game data loaded from USB or disc |
| 10 | Performance | Holds 60fps on real hardware in a full race |

### 0. Build tooling
- Devcontainer on a pinned `ps2dev/ps2dev` tag, with the repo mounted at the same
  path as on the Mac. Check whether the old `apk add make mpc1 mpfr4 gmp` is still needed.
- `build_and_run.sh` and `host_bridge.sh` brought across from `~/Development/ps2dev`,
  converted to the pinned image and same-path mounts.
- Gitignored `docs/hw/` plus a script that converts the hardware manuals to text
  (`pdftotext -layout`, from poppler).
- A placeholder ps2sdk target proving the loop: build → `.elf` → PCSX2 → log check.

### 1. Compiles and links
Done. The Dreamcast code went deeper than planned, so it took eight commits. Each
of the first six was checked against a KOS v2.2.1 Dreamcast build.
1. Replace `pvr_vertex_t` with a neutral `render_vertex_t` (mechanical).
2. Move the SH4 matrix code out of game files into `mtx.h`.
3. Add `render_begin_translucent()` for the opaque-to-translucent switch.
4. Move the Dreamcast sound code from `wipeout/sfx.c` to `src/sfx_dc.c`.
5. Remove KOS from `types.h` and game code.
6. Make `types_dc.c` platform-neutral as `src/types.c`.
7. ps2sdk Makefile and PS2 stubs; `wipeout.elf` links. Stubs log `TODO: <function>`.
8. Docs.

### 2. Boots headless
Done in PCSX2: logging to the PCSX2 log, EE-timer timing, a frame counter, and
game data from `host:`. It reaches the title screen and stays up. Not yet checked
on real hardware (no ps2link until stage 9).
- IOP module loading wasn't needed yet: PCSX2's `host:` works with the default
  modules. It's needed for pad (stage 4), audio, memory card and real-hardware
  file access.

### 3. 2D on screen
Done in PCSX2: gsKit setup, vsync (60fps), `render_push_2d` as flat rectangles with
stand-in colours per texture, and `make VIDEO=480i|480p|vga`. Still to check on real
hardware, including a thin black band at the bottom in VGA mode under PCSX2.
- 16:9 moved to stage 5: the Dreamcast port's letterbox mode (640×360) is checked in
  about a dozen places in menu and HUD code, and anamorphic needs its own layout and
  projection, which can only be judged with 3D on screen.

### 4. Pad input
Done in PCSX2: libpad with the BIOS pad modules, buttons and sticks mapped by position
to the Dreamcast layout, and the Dreamcast controller model skipped on PS2. Five Cross
presses from the title start a race on track02, which runs with the HUD in flat colours.
Not yet checked with a real pad on a real console.

### 5. 3D untextured
Done in PCSX2: track, scenery, ships and menu models draw as Gouraud-shaded
polygons with depth testing, near-plane and guard-band clipping, back-face culling,
sprites and screen shake; 16:9 anamorphic works. Races run at about 30fps.
Left for later: boost/item pads, which the Dreamcast draws twice with special
blending (stage 6), and performance (stage 10). The renderer builds its own GIF
packets, so drawing is already batched per 64 KB; the per-matrix batching for VU1
is part of stage 10.
- Anamorphic 16:9 (moved from stage 3): same framebuffer, 16:9 projection, HUD/2D
  aspect correction. Replace the Dreamcast-only letterbox mode and fix the
  out-of-bounds bug in `render_res_to_size()`.
- CPU transform, perspective, and near-plane clipping. The GS doesn't clip geometry,
  only scissors pixels, within a 4096×4096 guard band in 12.4 fixed point.
- Z-buffer. Follow the depth-test restriction in `CLAUDE.md`.
- Read: GS manual, GDC 2002 PS2 clipping paper.

### 6. Textures and blending
Done in PCSX2: textures from a VRAM cache, perspective-correct, with the game's
filtering setting; the six blend modes; boost pads at the PS1's double brightness,
as in wipeout-rewrite, in one pass. VGA became the
default build, for square pixels in screenshots.
- The 16-bit-only first version thrashed in races: about 1.6 MB of textures a
  frame against 1056 KB of VRAM, at 20fps.
- The paletted spike worked without game code changes: textures with 16 or 256
  colours become PSMT4/PSMT8 when created. In a race 181 of 206 are 4-bit, the
  race uploads 584 KB once, and it's back to 30fps.
- Not done: the Dreamcast's sky-texture column shifting (a fix for seams with
  filtering) and its per-track DECAL texturing; neither showed a problem.

### 7. Sound
- audsrv for sound effects and music. Go to SPU2 level only if audsrv isn't enough.
- Reverb in tunnels, as on the PS1 (missing in wipeout-rewrite): the SPU2 has
  hardware reverb. See `docs/FEATURES.md`.
- Extra, if time allows: the intro video. The IPU decodes MPEG in hardware.
- Read: ps2sdk IOP module source, ps2tek, PCSX2's SPU2 code.

### 8. Saves
- libmc in place of the VMU code.

### 9. Real hardware and packaging
- Checklist from earlier stages: DMA, alignment and timing (PCSX2 tolerates what
  hardware doesn't); the thin black band at the bottom in VGA mode under PCSX2.
- ps2link + ps2client over the network for development.
- Decide how a console finds the game data (it isn't redistributable): USB,
  a disc image the user builds, or `host:`.
- Output on the SDM-X53 via VGA.

### 10. Performance
- Profile first. A C + VU0 macro-mode CPU path may already reach 60fps.
- VU1 transform/clip microcode if needed. Reference: ps2gl's `vu1/` renderers.
- Texture loading: `render_texture_create()` finds each texture's colours to store
  it paletted (stage 6), work that `image.c` throws away when it expands the PS1's
  indices to 16-bit. Measure load times; if it matters, a paletted create call fed
  from `image.c` (or a precomputed table) skips it.

## Architecture decisions

### Keep the Dreamcast fork's game code, replace its render seam
The Dreamcast port is the base for game code: it carries real fixes (rescue-loop
infinite loops, out-of-bounds fixes) and memory trimming. Its render interface is
shaped for the PowerVR: game code fills a global `pvr_vertex_t vs[5]` and each
`render_tri()` / `render_quad()` call transforms and submits one polygon at once.
The GS and VU1 want batches instead.

### Two vertex types
- `render_vertex_t` is what game code writes: float position, UV and colour, before
  transform. It replaces `pvr_vertex_t` in game code and contains nothing
  hardware-specific.
- `gs_vertex_t` lives only inside the PS2 renderer: after transform, screen XYZ in
  12.4 fixed point, STQ, RGBAQ.
- They can't be one type: game code works before the transform, and the GS only
  accepts transformed fixed-point data. Naming the game-facing type after the GS
  would repeat the `pvr_vertex_t` leak.

### The renderer batches internally; the API stays the same
`render_tri()` / `render_quad()` append to a buffer instead of drawing. The buffer
flushes when the model matrix, texture or blend mode changes, and at frame end.
A matrix change is a natural batch boundary, which is the shape VU1 needs later,
so the VU1 rewrite stays inside `render_ps2.c`.

### SH4 code in game logic becomes C
The SH4 matrix assembly and `noxform` transforms become plain C first. VU0
macro-mode only where profiling shows it's needed.

## Open questions
- Full-height or half-height rendering for 480i. Full-height (640x448, the GS's
  FIELD mode, FFMD=0) is what gsKit does by default and what we use; half-height
  (640x224 per field, FRAME mode) halves VRAM and fill rate but is softer.
- Whether 800×600 VGA with 16-bit framebuffers is worth the fill rate and VRAM.
- How game data ships to a real console (stage 9).
