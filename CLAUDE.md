# wipeout-ps2

Goal: a native PlayStation 2 port of wipEout, running on real PS2 hardware.
Forked from jnmartin84/wipeout-dc (Dreamcast port, finished Oct 2025), which is
itself a port of phoboslab/wipeout-rewrite. As of Oct 2026 no other PS2 port of
wipeout-rewrite exists that we could find.

The plan, stage by stage, and the architecture decisions are in `ROADMAP.md`.
Read it before starting work on a stage.

## Current state (Oct 2026)
- Game code is still the Dreamcast port: Makefile, KOS dependencies, PVR renderer.
- Stage 0 (build tooling) is done: pinned container, `tools/` scripts, and a smoke
  test that runs in PCSX2. Next up: stage 1 in `ROADMAP.md`.
- An earlier tooling commit (devcontainer + watch_ps2.sh) was never pushed. It's kept
  on the local branch `archive/devcontainer-tooling`; stage 0 replaced it.
- Game data is not in the repo. It comes from the user's own copy of the original game.

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
- It is NOT fully platform-agnostic. The Dreamcast port leaked into 9 files:
  - `sfx.c`, `sfx.h`, `track.c`, `object.c`, `ship.c` and `hud.c` include KOS
    or `dc/` headers.
  - `hud.c`, `object.c`, `scene.c`, `ship.c`, `track.c`, `track.h` and `main_menu.c`
    use PVR types. Most of this is the global `pvr_vertex_t vs[5]`, which game code
    fills before calling `render_tri()` / `render_quad()`.
  - `object.c` also does some transforms itself (the `render_*_noxform` paths).
- Dreamcast-specific parts to replace:
  - `platform_dc.c` → PS2 platform layer (init, timing, main loop, file I/O)
  - `render_dc.c` → GS renderer (see the architecture section of `ROADMAP.md`)
  - `types_dc.c`, `asmfuncs.S` (repo root) → SH4-specific code. Rewrite in C
    first and optimise later.
  - `vmu.c`, `vmudata.h` → memory card saves (libmc)
  - `input.c` (Maple/DC pad) → libpad
  - `sndwav.c` → audsrv / SPU2
  - `dcpad.bin`, `sonic.raw` (repo root) → Dreamcast assets, not needed
- Interfaces to implement are in `platform.h`, `render.h`, `system.h`, `input.h`.
- Known bug: `render_res_to_size()` in `render_dc.c` has 2 entries but the
  `render_resolution_t` enum has 3, so `RENDER_RES_169L` reads out of bounds.
  Nothing uses it yet (`game.c` hard-codes `RENDER_RES_43`).

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
- Logging: `sio_puts()` reaches PCSX2's emuLog.txt. Plain `printf()` does NOT with
  the default settings. Sort out logging properly in stage 2.
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
