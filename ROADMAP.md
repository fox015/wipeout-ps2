# wipeout-ps2 roadmap

How the Dreamcast port becomes a native PS2 port: the stages, what "done" means
for each, and the architecture decisions behind them.

Each stage lands as one PR, with one commit per step inside it (see "How changes
land" in `CLAUDE.md`). Finished stages are tagged `stage-N`.

## Targets
- **60fps, locked to vsync**, on real hardware. (Higher isn't meaningful: the PS2
  outputs at 60Hz NTSC/480p/VGA or 50Hz PAL.)
- **Real hardware is a regular check, not only the last stage.** Boot on the console
  at the end of stages 2, 3, 6 and 7 at least. PCSX2 hides DMA, timing and
  alignment bugs.

## Stages

| # | Stage | Done when |
|---|---|---|
| 0 | Build tooling | A placeholder `main()` builds in the container and prints to PCSX2's log, through `build_and_run.sh` |
| 1 | Compiles and links | `wipeout.elf` links, with every PS2 layer stubbed |
| 2 | Boots headless | Logs show assets loading from `host:` and the frame counter rising, with no crash |
| 3 | 2D on screen | Title screen and menus visible (untextured) |
| 4 | Pad input | Can navigate menus and start a race |
| 5 | 3D untextured | Track and ships visible as flat/Gouraud polygons, correctly clipped and depth-tested |
| 6 | Textures and blending | The game looks right |
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
Commits, in order:
1. Replace `pvr_vertex_t` with a neutral `render_vertex_t` (mechanical; still the Dreamcast build).
2. Move the KOS / `dc/` includes and PVR usage out of the 9 game files and into the
   platform layer.
3. ps2sdk Makefile and stub `*_ps2.c` files. Stubs log `TODO: <function>` rather than
   being commented out, so the logs show what is missing.

### 2. Boots headless
- IOP module loading. Pad, audio, memory card and host file I/O all depend on it.
- `host:` file I/O for game data, `printf` logging, timing, and the main loop.

### 3. 2D on screen
- Framebuffers, vsync, `render_push_2d`.
- Display modes: 480i, 480p and VGA 640×480@60, each with 4:3 or anamorphic 16:9.
  Anamorphic means the same framebuffer and a 16:9 projection, at no VRAM cost.
  HUD/2D needs aspect correction. Drop the Dreamcast-only letterbox mode and fix the
  out-of-bounds bug in `render_res_to_size()`.

### 4. Pad input
- libpad. Small (`input.c` is about 300 lines) and needed to reach a race to test stage 5.

### 5. 3D untextured
- CPU transform, perspective, and near-plane clipping. The GS doesn't clip geometry,
  only scissors pixels, within a 4096×4096 guard band in 12.4 fixed point.
- Z-buffer. Follow the depth-test restriction in `CLAUDE.md`.
- Read: GS manual, GDC 2002 PS2 clipping paper.

### 6. Textures and blending
- Start with the existing 16-bit textures.
- **Spike:** keep textures in their original 4/8-bit paletted form (PSMT4/PSMT8).
  It's ¼ to ½ the VRAM of 16-bit, which matters with about 1 MB left for textures.
  Keep every texture in main RAM and upload what each frame needs.
- Map the six `render_blend_mode_t` modes onto the GS alpha formula.

### 7. Sound
- audsrv for sound effects and music. Go to SPU2 level only if audsrv isn't enough.
- Read: ps2sdk IOP module source, ps2tek, PCSX2's SPU2 code.

### 8. Saves
- libmc in place of the VMU code.

### 9. Real hardware and packaging
- ps2link + ps2client over the network for development.
- Decide how a console finds the game data (it isn't redistributable): USB,
  a disc image the user builds, or `host:`.
- Output on the SDM-X53 via VGA.

### 10. Performance
- Profile first. A C + VU0 macro-mode CPU path may already reach 60fps.
- VU1 transform/clip microcode if needed. Reference: ps2gl's `vu1/` renderers.

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
- Frame (full-height) or field rendering for 480i. Decide in stage 3.
- Whether 800×600 VGA with 16-bit framebuffers is worth the fill rate and VRAM.
- How game data ships to a real console (stage 9).
