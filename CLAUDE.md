# wipeout-ps2

Goal: a native PlayStation 2 port of wipEout, running on real PS2 hardware.
Forked from jnmartin84/wipeout-dc (Dreamcast port, finished Oct 2025), which is
itself a port of phoboslab/wipeout-rewrite. As of Oct 2026 no other PS2 port of
wipeout-rewrite exists that we could find.

The plan, stage by stage, and the architecture decisions are in `ROADMAP.md`.
Read it before starting work on a stage.

## Current state (Oct 2026)
- Stages 0–5 are done. In PCSX2 the pad drives the game from the title screen
  through the menus into a race, with 3D drawn as Gouraud-shaded polygons and 2D
  as flat rectangles. Textures are stand-in colours per texture index until
  stage 6. Menus run at 60fps, races at about 30fps (performance is stage 10).
  Sound and saves are stubs. Next up: stage 6.
- Not yet run on real hardware (needs ps2link, stage 9).
- An earlier tooling commit (devcontainer + watch_ps2.sh) was never pushed. It's kept
  on the local branch `archive/devcontainer-tooling`; stage 0 replaced it.
- Game data is not in the repo. It's the PS1 data from phoboslab's wipeout-rewrite
  data zip, unzipped by the user into the gitignored `wipeout/` folder in the repo
  (`wipeout/common`, `wipeout/textures`, `wipeout/track01`…, music as QOA).
  PCSX2's `host:` root is the folder the `.elf` is in, so the game reads it as
  `host:wipeout/...`. Never download or extract game data yourself.
- The controls page's 3D model of a Dreamcast pad (`dcpad.bin`) is Dreamcast-only
  (`#if defined(_arch_dreamcast)` in `main_menu.c`); on PS2 that page shows no model.

## How changes land
- One branch and one PR per roadmap stage. Inside a PR, each commit does one thing.
- Keep mechanical changes (renames, moves, type swaps) in separate commits from
  changes in behaviour.
- Every commit builds. main always works as far as the last merged stage.
- PR description: what it delivers, how to verify it (a log line or a PCSX2
  screenshot), and what is still stubbed.
- Merge without squashing. Tag each finished stage (`stage-0`, `stage-1`, ...).
- This repo is a GitHub fork. Always run `gh pr create --repo fox015/wipeout-ps2`,
  or the PR targets upstream jnmartin84/wipeout-dc.

## Source layout: where the platform seam is
- `src/wipeout/` holds the game logic from phoboslab. Change it as little as possible.
- Since stage 1, no file outside the `*_dc.*` platform files names KOS.
- Game code fills the global `render_vertex_t vs[5]` (32 bytes per vertex, same layout
  as the Dreamcast's `pvr_vertex_t`) before calling `render_tri()` / `render_quad()`.
  It must stay 32-byte aligned like `pvr_vertex_t`: struct sizes such as
  `track_face_t` (256 bytes) and `memcpy32()` depend on it. Losing the alignment in
  stage 1 shrank `track_face_t` and broke track physics (the craft tumbled) until
  it was restored in stage 5.
  `object.c` transforms vertices itself first (the `render_*_noxform` paths).
- Platform is selected with the compiler's own macros: `_arch_dreamcast` (KOS) and
  `_EE` (ps2sdk). Used in `mtx.h` and `sfx_platform.h`.
- Shared seams:
  - `mtx.h`: the "current matrix" (load once, transform many vectors), fast vector
    maths, `xform_all_verts`, `memcpy32`. `mtx_dc.h` is SH4 asm/KOS; `mtx_ps2.h` and
    `mtx_ps2.c` are plain C, to move to VU0 later.
  - `render.h`, `platform.h`, `system.h`, `input.h`, `wipeout/sfx.h`
    (per-sound platform state is the opaque `sfx_platform_t`).
- Platform files: `platform_*.c`, `render_*.c`, `sfx_*.c`, `mtx_*`. The PS2 ones are
  stubs that call `TODO()` (`todo_ps2.h`), which logs `TODO: <function>` once.
- Dreamcast-only, not built for PS2: `vmu.c`/`vmudata.h` (→ libmc, stage 8),
  `sndwav.c` (→ audsrv, stage 7), `asmfuncs.S`, `dcpad.bin`, `sonic.raw`.
  `input.c` is shared; the Dreamcast pad reading lives in `platform_dc.c`.
- PS2 pad: `pad_ps2.c` (libpad with the BIOS's `rom0:SIO2MAN`/`rom0:PADMAN`, port 1,
  analog mode). Buttons map to the Dreamcast layout by position: Cross = A (thrust,
  select), Circle = B, Square = X (fire), Triangle = Y (view), L2/R2 = triggers
  (airbrakes), L1/R1 = shoulders (unbound by default).
- Globals the game reaches with `extern` instead of a header, which each platform must
  define: `if_to_await`, `HUD_NO_TEXTURE`, `SetShake`, `ShakeScreen`,
  `last_five_tracks`, `wav_volume`, `view_mat`/`mvp_mat`/`vp_mat`.
- Builds: `make` is the PS2 build. `make -f Makefile.dc` is the Dreamcast build
  (KOS v2.2.1); stage 1 kept it compiling at every commit.
- Known bug (Dreamcast only): `render_res_to_size()` in `render_dc.c` has 2 entries
  but the `render_resolution_t` enum has 3, so `RENDER_RES_169L` reads out of bounds.
  The PS2 version maps both 16:9 values to 640x360.
- PS2 renderer (`render_ps2.c`): gsKit only sets up the display, clears (at the
  start of each frame) and flips. Everything else goes through `gs_ps2.c`, our
  GIF packet builder (A+D register writes, DMA'd when 64 KB fills or at frame
  end), because gsKit's primitives can't control depth writes (ZBUF.ZMSK).
  3D follows `render_dc.c`'s maths, with a general polygon clipper (near plane +
  GS guard band) in place of its hand-written quad cases. Depth is 1/w in a
  16-bit Z-buffer, GEQUAL.
- 16:9 is anamorphic as on the Dreamcast: layout 640x360, 16:9 projection, output
  fills all lines. Menu/HUD code checks `platform_screen_size().y == 360`.
- Known bug: `sfx_load()` in `sfx_dc.c` clears `nodes->data` in a loop without
  indexing, so only node 0 is reset. Harmless on the Dreamcast; don't copy it.

## PS2 constraints to keep in mind
- EE CPU: 32 MB RAM. The FPU is single-precision only, so a `double` falls back
  to slow software emulation. Avoid doubles.
- GS: 4 MB VRAM. It's reached only through DMA/GIF packets, never mapped into CPU memory.
  A 640×480 double-buffered 32-bit framebuffer plus a 16-bit Z-buffer uses about
  3 MB, which leaves about 1 MB for textures.
- Hardware errata from Sony's restrictions doc (`RESTRI_E.PDF`) that affect us:
  - Don't toggle the GS depth test off (ZTE=0). Use ZTE=1, ZTST=ALWAYS and
    ZBUF.ZMSK=1 instead.
  - Texture upload widths must be multiples of 8 pixels for PSMT8/PSMT4,
    4 for PSMCT16, 2 for PSMCT32.
  - Very short loops (6 instructions or fewer including the branch) can misbehave
    on the EE. Check that the toolchain works around this.
- gsKit gotcha: its default primitive blend, `GS_BLEND_BACK2FRONT`, keeps the
  destination for opaque pixels, so with `PrimAlphaEnable` on nothing shows.
  `render_init()` sets normal blending with `gsKit_set_primalpha()` after
  `gsKit_init_screen()` (setting `gs->PrimAlpha` beforehand had no effect).
- PCSX2 tolerates timing, DMA and alignment problems that crash real hardware.
  Test on real hardware regularly.

## Build environment (macOS, Apple Silicon)
- No native toolchain is installed (/usr/local/ps2dev does not exist).
  Builds run in Docker through Colima (`colima start --vm-type vz --mount-type virtiofs --cpu 6 --memory 8`).
- Image: `.devcontainer/Dockerfile`, pinned to `ps2dev/ps2dev:v2.0.0` by digest
  (native arm64, gcc 15.2.0). It only adds `make`, which the base image lacks.
  Bump the pin on purpose, never float on `latest`.
- Compiler: `mips64r5900el-ps2-elf-gcc`. Environment variables: PS2DEV, PS2SDK, GSKIT.
- The repo is mounted at the same path in the container as on the Mac, so paths in
  errors and debug info match the host. The devcontainer does the same.
- Builds run from the Mac with `docker run`. The `.elf` lands in the repo folder,
  where PCSX2 (a Mac app) opens it. A devcontainer is only for editor code intelligence.

## Build and run workflow (`tools/`)
- `tools/build_and_run.sh [--no-build] [--shot] <dir> [expect]`: builds the image from
  `.devcontainer/`, runs `make` in `<dir>` in the container, restarts PCSX2 with the
  `.elf`, copies emuLog.txt to `pcsx2_output.log` and waits for `expect` in it.
  `--shot` asks PCSX2 for a screenshot (F8) after a match.
- `tools/host_bridge.sh`: run on the Mac. Inside the devcontainer, write a target dir
  (and optionally an expect string on line 2) to `run_on_host.trigger`, and the bridge
  runs `build_and_run.sh --no-build` on it.
- `tools/smoke/`: minimal ps2sdk program. `tools/build_and_run.sh tools/smoke "SMOKE: ok"`
  proves the whole loop works.
- Logging: `platform_ps2.c` makes stdout and stderr line-buffered streams that
  write to the serial port, which PCSX2 copies to emuLog.txt. Use plain `printf()`.
  (`sio_puts()` appends its own newline, so avoid it for log lines.)
- Display output is a build option: `make VIDEO=480i|480p|vga` (default vga: 640x480, square pixels, matching the game's layout 1:1;
  480i is 640x448 field-rendered, scaled from 480 lines).  `build_and_run.sh` always builds the default; for another mode, run make with
  VIDEO in the container, then `build_and_run.sh --no-build`.
- Screenshots: the user granted the Claude app Accessibility permission, so
  `build_and_run.sh --shot` (F8 to PCSX2) works; read the PNG from
  `~/Library/Application Support/PCSX2/snaps/`. Screen Recording isn't granted.
- Drive PCSX2: `tools/pcsx2_input.sh hold k` presses Cross (keys follow PCSX2's
  Pad1 keyboard bindings, listed in the script), `tools/pcsx2_input.sh shot` takes a
  screenshot and prints its path. From the title, 5 Cross presses start a race
  (track02).
- Run the game: `tools/build_and_run.sh . "frames:"`. The log shows `load: <file>`
  per asset, `TODO: <function>` per stub on first use, `frames: N (t=Xs)` each second,
  and the game's own `Abort at ...` messages.
- PCSX2 on Mac is an Intel build running under Rosetta. ARMSX2 is a native alternative.
  PCSX2 uses the user's own BIOS dump, and host filesystem access (HostFs) is enabled.
- The original sandbox is `~/Development/ps2dev`. Its `samples/triangle` renders a gsKit
  triangle at 59.94fps, a known-good reference.

## Display hardware
- The user's real-hardware display is a Sony SDM-X53 LCD (1024×768 native) via the
  PS2 Linux kit VGA adapter. Sync-on-green works with it.
- 1024×768 can't be double-buffered in 4 MB of VRAM. Use VGA 640×480@60
  (800×600 with 16-bit buffers is a stretch goal).

## Hardware documentation (local only, never commit)
- Sony's PS2 Linux kit manuals: `~/Development/ps2/manuals/` (master copy).
  EE overview, EE user's manual, EE core user's manual, EE core instruction set,
  VU user's manual, GS user's manual, and hardware restrictions.
- These are copyrighted Sony documents and this repo is public. Keep them, and any
  text extracted from them, out of git.
- `tools/convert_manuals.sh` copies them into the gitignored `docs/hw/` as PDF + text
  (`pdftotext -layout`). Grep `docs/hw/*.txt` for a register or topic; read PDF pages
  as images when a table or diagram doesn't survive conversion.
- There is no IOP, SPU2, SIF, CD/DVD or memory card manual. For those, use ps2sdk's
  IOP module source, PCSX2's emulation source, ps2tek and psx-spx (the IOP is the PS1 CPU).
  Do not go looking for leaked SDK documents.

## Other local reference material (read for ideas, never copy into the repo)
- `~/misc/dell linux backup/home/fox/ps2linux/ps2backup/`:
  - `ps2gl/vu1/`: Sony's (SCEA) open-source GL layer, with production VU1 renderers
    including clipping and culling (`clip_cull.i`). Best reference for stage 10.
  - `vu_coding_contest_2003_*`: VU1 demos. The harness and macros are by SCEA/SCEE
    staff, `previous_demos` has entries from SCEA's developer-conference contests,
    the 2003 entries are amateur. One file is marked SCEI CONFIDENTIAL.
  - `vcl-1.28`: VCL, the VU assembly optimiser (openvcl is the open successor).
  - `sps2dev-0.4.0`: PS2 Linux direct-hardware kit. Concepts carry over, the API doesn't.
- `~/misc/dell linux backup/home/fox/Downloads/`: `final-gdc2002-clippingps2.pdf`
  (PS2 clipping, for stage 5) and `PS2Optimisations.pdf` (stage 10).
- `~/Development/ps2/3dmod/`: the user's own 2004 PS2 Linux code (DMA, pad, textures, VU1 in VCL).
- `~/Development/ps2/playstation2-linux.com.7z`: 1.7 GB archive of the PS2 Linux
  community site. Not yet inspected (needs 7z).

## References
- glampert/quake2-ps2: a modern, complete PS2SDK-only port. A good model for the
  renderer, input, audio and memory card code.
- jnmartin84/wipeout-dc commit history: shows how the code was adapted to a weak GPU.
