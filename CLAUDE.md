# wipeout-ps2

Goal: a native PlayStation 2 port of wipEout, running on real PS2 hardware.
Forked from jnmartin84/wipeout-dc (Dreamcast port, finished Oct 2025), which is
itself a port of phoboslab/wipeout-rewrite. As of Oct 2026 no other PS2 port of
wipeout-rewrite exists that we could find.

The plan, stage by stage, and the architecture decisions are in `ROADMAP.md`.
Read it before starting work on a stage.

## Current state (Oct 2026)
- Stages 0–8 are done. In PCSX2 the pad drives the game from the title screen
  through the menus into a race, fully textured and blended, with sound effects,
  music and tunnel reverb, and settings and records save to the memory card.
  Stages 10.1 (profiling and cutting the renderer's work), 10.2 (REGLIST
  GS packets) and 10.3 (game logic: batched sound commands, music streamed
  by the IOP) are done: menus run at 60fps; in PCSX2 a lap of any circuit
  averages 103–137fps uncapped, with 0.0–0.4% of frames over budget
  (performance is stages 10–12).
  Next up: the rest of stage 9 (living with ps2link, game data on the SSD's
  exFAT partition, standalone boot); see ROADMAP.md.
- Runs on the user's fat PS2 under ps2link from `host:` (stage 9.1, branch
  `stage-9/real-hardware`): 58fps in a hand-driven race.
- An earlier tooling commit (devcontainer + watch_ps2.sh) was never pushed. It's kept
  on the local branch `archive/devcontainer-tooling`; stage 0 replaced it.
- Game data is not in the repo. It's the PS1 data from phoboslab's wipeout-rewrite
  data zip, unzipped by the user into the gitignored `wipeout/` folder in the repo
  (`wipeout/common`, `wipeout/textures`, `wipeout/track01`…, music as QOA,
  plus the `.spu` music the user makes with `tools/convert_music.sh`).
  PCSX2's `host:` root is the folder the `.elf` is in, so the game reads it as
  `host:wipeout/...`. Never download or extract game data yourself.
- The controls page's 3D model of a Dreamcast pad (`dcpad.bin`) is Dreamcast-only
  (`#if defined(_arch_dreamcast)` in `main_menu.c`); elsewhere it shows the PS1 pad
  from the game data (`pad1.prm`), as wipeout-rewrite does.

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
- Dreamcast-only, not built for PS2: `vmu.c`/`vmudata.h` (VMU saves),
  `sndwav.c`, `asmfuncs.S`, `dcpad.bin`, `sonic.raw`.
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
  GIF packet builder (register writes as A+D, each polygon as one REGLIST tag:
  PRIM then 24 bytes a textured vertex; DMA'd when 64 KB fills or at frame
  end), because gsKit's primitives can't control depth writes (ZBUF.ZMSK).
  `draw_projected()` writes vertices straight into the packet (`gs_prim()`).
  3D follows `render_dc.c`'s maths, with a general polygon clipper (near plane +
  GS guard band) in place of its hand-written quad cases. Depth is 1/w in a
  16-bit Z-buffer, GEQUAL.
  Since stage 10.1: track sections and scenery outside the view frustum are
  skipped (`render_box_visible()`, called from `track.c` and `scene.c`); a
  polygon inside the guard band is drawn straight from `vs[]` without the
  clipper; GS state is sent only when it changes; the renderer, `mtx_ps2.c` and
  `gs_ps2.c` build with `-O3 -ffast-math`. All drawing goes over PATH3 (the EE
  builds packets, DMA channel 2 sends them to the GIF; see ROADMAP.md).
- PS2 textures: every texture stays in RAM, padded to powers of two as on the
  Dreamcast (game code divides UVs by `render_texture_padsize()`). The game hands
  over 16-bit colours, but nearly all textures have 16 or 256 colours at most, so
  `render_texture_create()` stores them as PSMT4/PSMT8 indices plus a 16-bit CLUT.
  VRAM after gsKit's buffers is a cache, emptied when full; uploads are logged
  once a second (`textures: N uploads ...`). A race on track02 uploads 584 KB once.
  Textured colours are halved, since the GS modulates with 128 as 1.0.
- Blend modes map onto the GS alpha formula exactly, except two only the Dreamcast
  renderer uses. Boost/item pads have PS1-scale colours (128 is 1.0, as in
  wipeout-rewrite), which the GS's modulate does natively: they're drawn unhalved
  in one pass, where the Dreamcast drew them twice with blending.
- PS2 sound (`sfx_ps2.c`, IOP modules loaded from the ELF by `iop_ps2.c`:
  libsd, ps2snd, iomanX and our own `iop/music/`):
  - Sound effects: `wipeout.vb` (PS1 ADPCM) goes into sound RAM once; each effect
    is one of core 1's 24 hardware voices. Voice commands are queued and sent
    to ps2snd's IOP server as one batch an RPC (`PS2SND_ProcBatch`, at most 15
    commands) at frame end, without waiting; only one batch is in flight.
    Calls that need an answer (allocating a voice, reading an address) flush
    the queue first. Loops patch the sample's last block to repeat.
  - Music: streamed by our IOP module (`iop/music/music.c`, built into the ELF
    as `music_irx`) on core 0's voices 0 and 1, so the EE does no music work
    beyond starting tracks and setting the volume (one RPC each; it asks every
    30 frames whether the track ended). The files are SPU2 ADPCM `.spu`, made
    from the QOA ones by `tools/convert_music.sh` into the gitignored
    `wipeout/music/`; the game's `.adpcm` paths become `.spu`. The module reads
    them through iomanX (so the SSD's exFAT partition will work too) into
    two halves of a buffer per channel, refilled on the SPU2 interrupt,
    uploaded on SPU2 DMA channel 1 (ps2snd uses channel 0). Core 1 mixes core 0 in (MMIX), dry and
    into the reverb.
  - Reverb: core 1's Hall effect, faded in by `sfx_set_reverb()`, which `race.c`
    calls each frame (sections with more than 4 faces, as on the PS1).
  - Gotcha: a libsd init (ps2snd's `sceSdInit()`) resets all voices, volumes,
    transfers and the SPU2 interrupt handler, so our settings come after it and
    the music module sets its handler with every track.
- PS2 saves (`memcard_ps2.c`): `platform_load_userdata()`/`platform_store_userdata()`
  use libmc on the card in slot 1 (BIOS MCMAN/MCSERV, loaded by `iop_ps2.c`),
  folder `/WIPEOUT-PS2`. It also gets `icon.sys` and `icon.ico`, written when
  missing: the Feisar converted at runtime from the loaded model and textures
  (`render_ps2_texel()` in `render_ps2.h` reads texels back), never stored in
  the repo. The emulator's slot 1 card must be inserted and formatted (see Emulator below).
  Icon triangles wind the other way from the game's.
- 16:9 is anamorphic as on the Dreamcast: layout 640x360, 16:9 projection, output
  fills all lines. Menu/HUD code checks `platform_screen_size().y == 360`.
- Known bug: `sfx_load()` in `sfx_dc.c` clears `nodes->data` in a loop without
  indexing, so only node 0 is reset. Harmless on the Dreamcast; don't copy it.

## PS2 constraints to keep in mind
- EE CPU: 32 MB RAM. The FPU is single-precision only, so a `double` falls back
  to slow software emulation. Avoid doubles.
- GS: 4 MB VRAM. It's reached only through DMA/GIF packets, never mapped into CPU memory.
  A 640×480 double-buffered 32-bit framebuffer plus a 16-bit Z-buffer uses about
  3 MB, which leaves about 1 MB for textures (1056 KB in VGA mode).
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
- Image: `.devcontainer/Dockerfile`, pinned by digest to
  `ps2dev/ps2dev@sha256:769c4dd6…d02d` (ps2dev:latest built 3 Nov 2026,
  native arm64, gcc 15.2.0). It only adds `make`, which the base image lacks.
  Bump the pin on purpose, never float on `latest`.
- It's the same pin as the shared toolchain for all the user's PS2 projects,
  `~/Development/ps2-toolchain` (its README says how to bump). Bump both
  together, and quote `tools/bench.sh all` before and after in the commit.
  The previous pin, `ps2dev/ps2dev:v2.0.0` (May 2026), had a ps2-packer that
  left a packed section's trailing zero bytes unwritten (fixed in
  ps2dev/ps2-packer#46). wipeout-ps2 doesn't pack its ELF, so it wasn't hit.
- Compiler: `mips64r5900el-ps2-elf-gcc`. Environment variables: PS2DEV, PS2SDK, GSKIT.
- The repo is mounted at the same path in the container as on the Mac, so paths in
  errors and debug info match the host. The devcontainer does the same.
- Builds run from the Mac with `docker run`. The `.elf` lands in the repo folder,
  where PCSX2 (a Mac app) opens it. A devcontainer is only for editor code intelligence.

## Build and run workflow (`tools/`)
- `tools/build_and_run.sh [--no-build] [--shot] <dir> [expect]`: builds the image from
  `.devcontainer/`, runs `make` in `<dir>` in the container, restarts the emulator with the
  `.elf`, copies emuLog.txt to `pcsx2_output.log` and waits for `expect` in it.
  `--shot` asks the emulator for a screenshot (F8) after a match.
- `tools/host_bridge.sh`: run on the Mac. Inside the devcontainer, write a target dir
  (and optionally an expect string on line 2) to `run_on_host.trigger`, and the bridge
  runs `build_and_run.sh --no-build` on it.
- `tools/smoke/`: minimal ps2sdk program. `tools/build_and_run.sh tools/smoke "SMOKE: ok"`
  proves the whole loop works.
- Logging: `platform_ps2.c` makes stdout and stderr line-buffered streams that
  write to the serial port, which the emulator copies to its emulog.txt. Use plain `printf()`.
  (`sio_puts()` appends its own newline, so avoid it for log lines.)
- Display output is a build option: `make VIDEO=480i|480p|vga` (default vga: 640x480, square pixels, matching the game's layout 1:1;
  480i is 640x448 field-rendered, scaled from 480 lines).  `build_and_run.sh` always builds the default; for another mode, run make with
  VIDEO in the container, then `build_and_run.sh --no-build`.
- Screenshots: the user granted the Claude app Accessibility permission, so
  `build_and_run.sh --shot` (F8 to the emulator) works; read the PNG from
  `~/Library/Application Support/ARMSX2/snaps/` (`PCSX2/snaps/` with `EMU=pcsx2`). Screen Recording isn't granted.
- Drive the emulator: `tools/pcsx2_input.sh hold k` presses Cross (keys follow its
  Pad1 keyboard bindings, listed in the script), `tools/pcsx2_input.sh shot` takes a
  screenshot and prints its path. From the title, 5 Cross presses start a race
  (track02).
- Profiling: `prof_ps2.c` reads the EE's cycle counter (COP0 Count) and logs
  once a second. `prof:` gives ms per frame (work, render, transform, DMA wait,
  final flush, vsync), polygon and vertex counts, KB sent to the GS, uncapped
  fps and frames over budget; `depth:` gives polygons by depth band and the
  camera's track section. In PCSX2 the counter is the emulator's estimate:
  compare runs, don't quote them as hardware figures. Render time includes
  `draw_vs()`'s own entry and exit only since stage 10.2 (it's timed from its
  callers); a change to what gcc inlines there shows up as render time now.
- Benchmarks: `tools/bench.sh` (one lap of track02, about 2 minutes) or
  `tools/bench.sh all` (one lap of every circuit). It builds with `make BENCH=1`
  or `BENCH=all`: the game races by itself with the AI driving, the camera
  behind the ship, no weapons, a fixed seed and a fixed 1/60s tick, so every
  run is the same race (`src/bench_ps2.h`). It prints `bench:` lines for each
  intro and lap (with KB sent to the GS), then rebuilds the normal ELF. Nobody
  needs to touch PCSX2. Performance commits and PRs quote it before and after.
  `tools/bench.sh shot N` freezes track02's lap at frame N (`make BENCH=1
  BENCH_FREEZE=N`) and takes a screenshot: the same frame in any build, to
  check a change draws the same. Compare as BMPs (`sips -s format bmp`); the
  same build differs from itself by about 2–3k bytes in the billboards.
- GS dumps: `tools/gs_dump.sh N` freezes the same lap frame and saves a GS dump
  (Shift+F8; ARMSX2 writes it as `.gs.zip`, which the tools read) and a screenshot into the gitignored `gsdump/`.
  `tools/gs_tools.py stats|tris|zsim|clut|... DUMP` runs ps-probe, the user's
  local toolset for this kind of analysis (outside this repo,
  `~/Development/ps-probe`, or `$PSPROBE`; its own `.venv`), with our settings
  from `tools/psprobe.toml`: gsKit's XYOFFSET and SCISSOR, 640x480, one vsync,
  FBP 0, fans labelled by winding. It reports what the GS is sent, per
  triangle, palette loads, and a depth replay for hidden polygons. Counts from
  a dump don't depend on the emulator's timing. Settings are options, never
  code edits; ps-probe's other tools (PINE, VU decoder, VIF walker) are
  `psprobe ps2 ...` in its venv.
- Run the game: `tools/build_and_run.sh . "frames:"`. The log shows `load: <file>`
  per asset, `TODO: <function>` per stub on first use, `frames: N (t=Xs)` each second,
  and the game's own `Abort at ...` messages.
- Emulator: ARMSX2 (`/Applications/ARMSX2.app`), PCSX2's native ARM64 fork, is the
  default for every script (`tools/emu.sh`). PCSX2 on Mac is an Intel build under
  Rosetta, at about 70% of real time; `EMU=pcsx2` before any script picks it, e.g. to
  check a surprising result. Both use the user's own BIOS dump (ARMSX2 reads PCSX2's
  `bios/` folder), HostFs and the same Pad1 keys; logs are `logs/emulog.txt` in each
  one's Application Support folder. Their timing estimates differ, so compare
  benchmarks on the same emulator: figures up to stage 10.3 are from PCSX2.
  The user's Psychonauts project uses ARMSX2 too, and both emulators currently have
  no card in slot 1 (`Slot1_Filename` is empty), so saves log `memcard: no formatted
  PS2 memory card`. ARMSX2's `memcards/` has copies of PCSX2's cards.
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
    including clipping and culling (`clip_cull.i`). Best reference for stage 12.
  - `vu_coding_contest_2003_*`: VU1 demos written in VCL or VU assembly. The
    harness is by SCEE staff (2003), for the 2003 PS2 Linux contest, which was
    open to amateurs only; `_entries` holds its entries. `previous_demos` comes
    from an earlier (2002) contest, whose entries include SCEA-copyrighted demos
    and one by a professional developer at Monolith. One file is marked SCEI
    CONFIDENTIAL.
  - `vcl-1.28`: VCL, the VU assembly optimiser (openvcl is the open successor).
  - `sps2dev-0.4.0`: PS2 Linux direct-hardware kit. Concepts carry over, the API doesn't.
- `~/misc/dell linux backup/home/fox/Downloads/`: `final-gdc2002-clippingps2.pdf`
  (PS2 clipping, for stage 5) and `PS2Optimisations.pdf` (stage 10; `006.ppt`
  is the same deck with speaker notes). Also `rgreen_procedural_renderinggdc2001.pdf`
  (writing VU1 code), `TextureSwizzling.pdf` (faster PSMT4/PSMT8 uploads, unread)
  and `ps2_normalmapping.pdf` (IO Interactive, unread). `nsce_presentation.ppt`
  there is a Sony internal planning deck: confidential, don't use it.
- `~/Development/ps2-talks/`: Ostler (SCEE), "The Primal Seas: Water on PS2",
  SIGGRAPH 2003 (portal/octree visibility, geometry generated on VU1).
- `~/scratch/psychonauts/notes/`: the user's Psychonauts PS2 investigation.
  `FOR_WIPEOUT.md` is its summary for us (VU1 renderer lessons, GS waste,
  emulator gotchas, and a list of every PS2 talk it found or looked for, with
  links); `FINDINGS.md` is the detail. Its figures are from emulators, GS dumps
  and save states, not hardware.
- `~/Development/ps2/3dmod/`: the user's 2004 PS2 Linux experiments (DMA, pad, textures,
  a little VU1 in VCL). Dabbling, not a reference: for VU code use ps2gl and the contest demos.
- `~/Development/ps2/playstation2-linux.com.7z`: 1.7 GB archive of the PS2 Linux
  community site. Not yet inspected (needs 7z).

## References, ranked by quality
Prefer higher tiers, above all for VU microcode and other assembly. Lower tiers
are for ideas only, checked against a higher one before anything relies on them.
Local material (paths above) is read for ideas, never copied.
1. **Sony's own engineers.** The hardware manuals; ps2gl and ps2stuff (SCEA,
   open-sourced; maintained as ps2dev/ps2gl and ps2dev/ps2stuff); VCL (SCEA);
   Colin Hughes' GDC 2002 "PlayStation 2 Clipping" and George Bain's AGDC 2002
   "PS2 Programming Optimisations" (both SCEE); Lionel Lemarié's "Reaching for
   the Limits of PS2 Performance" (SCEE 2003, statistics from shipped games,
   web copy at retrocdn.net); Robin Green's GDC 2001 "Procedural Rendering on
   PlayStation 2" (SCEA); Dean & Breugelmans' "Texture Compression" (SCEE,
   2001, on Scribd); the VU contest harness (SCEE)
   and the SCEA demos in `previous_demos` (`invite`, `slow_motion`, `tree`).
2. **Proven open source, widely used.** ps2sdk and gsKit (we build on them);
   PCSX2's source (how the hardware behaves, and how VU timing is emulated);
   ps2tek and psx-spx; ps2dev/openvcl (VCL's open successor);
   glampert/quake2-ps2 (a complete PS2SDK-only port: renderer, input, audio,
   memory card; not VU-heavy); jnmartin84/wipeout-dc's history (how this code
   was adapted to a weak GPU).
3. **Professional but one-off.** `marionette` in `previous_demos` (by a
   Monolith developer).
4. **Hobbyist.** h4570/tyra (an engine rendering through VU1, with VU1
   clipping); blog posts; the 2003 contest's amateur entries and other
   contest entries such as `peewee` (demoscene); the user's 2004
   `3dmod` experiments; anything linked from awesome-lists.
