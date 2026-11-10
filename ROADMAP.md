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
| 7 | Sound ✅ | Sound effects and music in menus and races |
| 8 | Saves ✅ | Settings and best times survive a power cycle on a memory card |
| 9 | Real hardware and packaging | Runs on a real PS2 without ps2link, with game data loaded from the exFAT partition on its internal drive |
| 10 | Performance: the C path ✅ (10.1–10.3) | The EE's C renderer (PATH3) is as fast as it gets, in sub-stages 10.1–10.3 |
| 11 | Performance: VU0 macro mode | Vertex maths on VU0, measured on real hardware |
| 12 | Performance: VU1 renderer | Transform, clip and packets on VU1 (PATH1); 60fps with headroom on real hardware in a full race |
| 13 | Our enhancements | neGcon support and a video options menu (see `docs/FEATURES.md`) |

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
is part of stage 12.
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
Done in PCSX2, heard by the user: sound effects, music and tunnel reverb.
- Sound effects went to SPU2 level after all: `wipeout.vb` is the PS1's ADPCM,
  which the SPU2 plays as it is, so it's uploaded to sound RAM once and each
  effect is a hardware voice (ps2snd → libsd). audsrv has no per-voice pitch.
- Music: the QOA files are read from the host as they play, decoded on the EE
  by a thread and streamed to audsrv as PCM. (Replaced in stage 10.3: the IOP
  streams SPU2 ADPCM, and audsrv is gone.)
- Reverb in tunnels, as on the PS1 (missing in wipeout-rewrite): the SPU2's
  hardware Hall reverb, faded in where the camera's track section has more than
  4 faces. It takes the music too.
- audsrv mutes core 0 and every libsd init resets everything, so effects use
  core 1 and the init order is fixed (see `sfx_ps2.c`).
- Not done: the intro video (the IPU decodes MPEG in hardware).

### 8. Saves
Done in PCSX2: settings and records save to the memory card in slot 1 and
load at boot; a setting changed in the menus survived a reboot.
- libmc with the BIOS's MCMAN/MCSERV (they go with the BIOS SIO2MAN the pad
  uses). The game saves on scene changes, as on the Dreamcast; each libmc call
  is waited for, so a save holds up that frame.
- No card or an unformatted one: logged, and the game runs without saving.
  It doesn't offer to format a card.
- The browser icon is the Feisar, converted on the PS2 from the model and
  textures the game loaded from the player's data, so no game artwork is in
  the repo.

### 9. Real hardware and packaging
The console is the user's fat PS2 (SCPH-30002 R, PAL) with the network adapter
and an SSD on a PATA-to-SATA board, on the SDM-X53 through the PS2 Linux kit's
VGA adapter. PS2BBL on the memory card launches ps2link (Square). Each
sub-stage is its own branch, PR and tag (`stage-9.1` ...).
- **9.1 First boot.** ✅ on hardware, not yet merged. `make LOG=net` sends the
  log over ps2link's network stack to `ps2client listen` on the Mac (`LOG=sio`,
  the default, writes to the serial port, which only an emulator reads). The
  game runs from `host:` through ps2client: menus, races, VGA on the SDM-X53,
  pad, sound effects with reverb, saves on the real card. One fault PCSX2
  hides: gcc's 64-bit stores into the 32-byte-aligned track faces raised an
  address error at `mem_bump()`'s 4-byte alignment, so the faces are now
  aligned to 32. First profile, a hand-driven race: 58fps average, work
  11.4 ms a frame, 3.7% of frames over budget (PCSX2: 0.0–0.4%).
- **9.2 Living with ps2link.** The game doesn't reset the IOP, so ps2link's
  modules (dev9, SMAP, the network stack, its reset-button handler) stay
  loaded under it and share IOP memory with ours. Still to do:
  - `ps2client reset` and the console's reset button crash ps2link (a newlib
    reent bug and I/O in the reset-button interrupt). A fixed ps2link build
    exists outside this repo; install it on the card, then test both resets
    mid-race.
  - The game's heap and stack (ps2sdk's linkfile defaults: the stack at the
    top of RAM, the heap up to it) cover a high-loading ps2link
    (0x01EE8000). Set `_stack` and `_heap_size` below it for ps2link builds,
    so a reset can re-exec ps2link.
  - Listen for music: the first runs logged no music lines.
- **9.3 Game data on the SSD.** The game data isn't redistributable, so it
  lives on the user's drive. The SSD is a PC-style MBR disk shared with
  PS2 Linux (swap, `/home`, `/usr`, `/`), so an APA/PFS drive read as `pfs0:`
  would mean repartitioning it, which is ruled out: the Linux install must
  survive. Instead, partition 4 is exFAT ("PS2GAMES"), which OPL 1.2 already
  reads on this console. The game reads it through ps2sdk's block-device
  stack: `ata_bd.irx` (the ATA drive as a block device, on dev9), `bdm.irx`
  (block devices and MBR partitions) and `bdmfs_fatfs.irx` (FAT/exFAT). That
  registers an iomanX device, so everything that opens game files, on the EE
  and the IOP (the music stream), still goes through iomanX: the EE's newlib
  through fileXio. To check first: the device name and unit the partition
  appears as, that this ps2sdk's fatfs is built with exFAT, and that dev9 is
  not loaded a second time when ps2link has already loaded it.
  The data goes in a `WIPEOUT/` folder, copied with the SSD on the Mac;
  never write to the Linux partitions. Then measure load times (PCSX2:
  344–425 ms a race over `host:`) and listen for music dropouts while levels
  load.
- **9.4 Standalone.** Launched from PS2BBL or OPL without ps2link: reset the
  IOP when ps2link isn't there and load our own modules, find the data
  folder from `argv[0]`, keep `host:` as the development fallback.
- **9.5 Hardware checklist and benchmarks.**
  - DMA, alignment and timing: any other `aligned(16/32)` struct in
    `mem_bump()` memory is a candidate for the fault 9.1 found.
  - VGA on the SDM-X53: the thin black band at the bottom seen under PCSX2,
    and the picture's position and size.
  - Saves on a real card: save times (each libmc call waits) and the browser
    icon.
  - GS time, which only hardware shows: PCSX2 charges next to nothing for
    fill, palette loads, TEXFLUSH or primitive setup (the Psychonauts
    investigation below found the same), so a frame that's fine in PCSX2 can
    still be GS-bound on a console. Compare render time with and without
    drawing.
  - Benchmarks: ✅ (10 Nov 2026). `make BENCH=all LOG=net` builds of main
    (e77c064) and of the `stage-8-baseline`, `stage-10.1`, `stage-10.2` and
    `stage-10.3` points, each with 9.1's alignment fix, the network log and
    the `bench.txt` writer added, all built with the same toolchain image.
    Run back to back from `host:` with `ps2client reset` between runs. Two
    runs of main agree to 0.01–0.03 ms a frame per circuit. Over all seven
    circuits, weighted by frames (budget 16.7 ms):

    | Build | Lap work a frame | Lap frames over budget | Intro work | Intro frames over budget |
    |---|---|---|---|---|
    | stage 8 | 30.65 ms | 92.9% | 35.01 ms | 90.3% |
    | 10.1 | 13.40 ms | 20.1% | 14.15 ms | 29.0% |
    | 10.2 | 12.75 ms | 14.5% | 13.67 ms | 21.5% |
    | 10.3 | 11.09 ms | 6.4% | 12.81 ms | 15.5% |
    | main | 11.16 ms | 6.8% | 12.90 ms | 16.3% |

    - track02's lap: 25.47 → 11.68 → 11.09 → 9.40 → 9.47 ms. ARMSX2
      estimates 5.37 ms, so hardware takes about 1.75× its figure. Laps run
      at 81–106 fps uncapped (PCSX2: 103–137).
    - 10.2's REGLIST packets, slightly slower in PCSX2, save 0.65 ms on
      hardware.
    - In 10.1 and 10.2 most laps have a 56–173 ms worst frame (the QOA music
      decode); from 10.3 none goes past 40 ms.
    - main is 0.07 ms slower than 10.3, more than the run-to-run noise. Not
      investigated.
    - What's left over budget is whole sections, not spikes: the slowest
      average 18–20 ms (track02 215–224, track04 211–215, track12's
      start line, track10 258–263). Worst laps: track10 (15% over budget),
      track12 (10%); worst intro: track08 (74%). This is the load stages 11
      and 12 are for.

### 10. Performance: the C path (PATH3)
Every drawing packet is built by the EE in C and DMA'd from main RAM straight to
the GIF: PATH3 (see "How data reaches the GS" below). Each sub-stage is its own
branch, PR and tag (`stage-10.1` ...). Its PR gives before/after ms per frame
from the benchmarks, measured in PCSX2. After stage 9, each tag is measured again
on hardware, for true figures.
- **10.1 Profile and cut the work.** A frame profiler on the EE's cycle counter
  (with uncapped fps, frames over budget and polygons by depth). Frustum culling
  of track sections and scenery; a fast path that skips the clipper for polygons
  inside the guard band; drawing straight from `vs[]`; cached GS state;
  `-O3 -ffast-math` on the renderer. A benchmark build that races one lap
  by itself, the same race every run (`tools/bench.sh`).
  Result in PCSX2 on track02, against stage 8 with the same benchmark: the
  intro's work per frame went from 38.8 to 13.1 ms (26 to 76fps uncapped),
  the lap's from 22.6 to 8.2 ms (44 to 121fps uncapped), and frames over
  budget in the lap from 71.5% to 1.0%. Across all seven circuits a lap now
  averages 95–121fps uncapped, with 0.6–4.8% of frames over budget; the
  heaviest intro is track08's (16.2 ms, 46% of frames over).
- **10.2 Leaner GS packets.** REGLIST GIF tags instead of A+D: 24 bytes per
  textured vertex instead of 48, half of which were register numbers. Written
  straight into the packet, without the intermediate `gs_vertex_t` copy; tags
  and register lists built once. Also: the back-face test computed relative to
  the first vertex (it had float cancellation at GS coordinates around 32000),
  the renderer timed from `draw_vs()`'s callers (its entry and exit had been
  left out of render time), and `tools/bench.sh shot N` for frame-exact
  screenshots.
  Result in PCSX2: a third less data to the GS (track02 lap 169 to 114 KB a
  frame), but each lap 0.13–0.28 ms slower (track02 8.23 to 8.37 ms): at 10
  MB/s, bandwidth was never the bottleneck, and gcc's inlining around the new
  code made `draw_vs()`'s register saves bigger. A lap now averages 93–119fps
  uncapped across the circuits, with 0.9–5.4% of frames over budget. Real
  hardware decides whether the smaller packets pay.
- **10.3 Game logic and loading.** The game's own work, profiled: in a
  track02 lap, the sound commands (one blocking RPC to the IOP each, about
  50us, 750–860 a second) and the music decode (4.6 ms in one burst per QOA
  frame, behind nearly every frame over budget) were the costs worth cutting.
  - Sound commands are queued and sent to ps2snd's IOP server as batches
    (`PS2SND_ProcBatch`, which its IOP side already handles) at frame end,
    without waiting.
  - Music is SPU2 ADPCM (`tools/convert_music.sh` converts the QOA files,
    30–35 dB SNR against them), streamed by our own IOP module
    (`iop/music/`) on two core 0 voices: no music work on the EE at all.
    ps2snd's own streaming did the same but leaks a thread per track.
  - Sprites set their own UVs (they had used whatever `vs[]` last held).
  - Texture loading, measured: a race loads in 344–425 ms in PCSX2, of which
    `render_texture_create()` is 99–115 ms, LZSS 85–103, reading files 82–114
    and TIM decoding 12–15. Not worth changing upstream `image.c` for now;
    measure again on hardware with the data on the SSD (stage 9.3).
  - Tried and set aside: an object path that skips `vs[]` (no gain).
  Result in PCSX2, against 10.2: each lap 1.0–1.4 ms faster (track02 8.38 to
  7.32 ms, 119 to 137fps uncapped); across the circuits a lap averages
  103–137fps uncapped, with 0.0–0.4% of frames over budget (was 0.9–5.4%).
  Intros are 0.4–0.9 ms faster; track08's is still the heaviest (15.7 ms,
  39% of frames over).

Stage 10 changes nothing visible. Drawing less (a shorter draw distance) waits
until stage 12: the progression should show the same picture drawn faster, and
real hardware plus a VU1 renderer may leave it unnecessary. See stage 13.

### 11. Performance: VU0 macro mode
After stage 9, so its figures come from hardware: PCSX2 times VU0 instructions,
FPU latencies and cache misses only roughly, and this stage's gain is mostly in
exactly those. Still PATH3: VU0 only does the EE's maths, inline (as COP2).
- Vertex transform (`vmulax`/`vmadda`... four instructions for a 4×4 matrix),
  outcodes with `vclip`, projection with `vdiv`. In `mtx_ps2.c` and the outcode
  test in `render_ps2.c`.
- VU0 loads 16-byte aligned vectors. `render_vertex_t`'s position isn't one, so
  track quads need their positions copied first; models' `vec4_t` are fine.
- Also try `-Os` against `-O3` on hardware: Lemarié (SCEE, 2003) found
  shipped games gained up to 10% from small code, as EE instruction-cache
  misses were their main slowdown. PCSX2 can't tell (it barely models them).

### 12. Performance: VU1 renderer (PATH1)
- VU1 microcode transforms, clips and builds GS packets itself, and kicks them to
  the GIF with `XGKICK` (PATH1), in parallel with the EE. The EE sends a matrix
  and its vertices per batch through VIF1. Textures keep going over PATH3.
- Lessons from the user's Psychonauts PS2 investigation (a separate project
  that measured a shipped game's VU1 renderer from GS dumps, save states and
  its microcode; emulator and static figures, none from hardware):
  - One fused loop per vertex (unpack, transform, divide, clip flags, FTOI4,
    store), not passes chained over VU memory. Each of Psychonauts' passes was
    as good as Sony's samples, but chaining them cost 2–4× the 15–20 cycles a
    vertex Sony called typical. A few specialised programs (e.g. unclipped,
    clipped), chosen per batch on the EE.
  - Cull in layers: per track section on the EE before anything is sent, a
    bounding-sphere test per batch on VU1 (about 50 cycles), then trivial
    rejects per triangle with ADC. Clip only triangles that cross the guard
    band (we already do), without transforming their vertices again.
  - ADC restarts strips and drops rejected triangles without duplicate
    vertices (42% of Psychonauts' triangles were zero-area strip joins).
  - Double-buffer VU memory (XTOP/XITOP) and kick early, so one batch's
    XGKICK overlaps the next batch's maths. Emulators don't show XGKICK
    stalls; measure them on hardware.
  - Textures stay on PATH3 (Bain, AGDC 2002: PATH1 stalls while PATH2
    transfers), with no FLUSHA per texture change.
  - Track and ship meshes precompiled as VIF UNPACK streams (16-bit
    positions, packed UVs and colours), the EE only writing headers and DMA
    `ref` tags; referenced data aligned to 8 qwords.
  - A static timing model of the microcode first: the Psychonauts session's
    `vu.py` predicted the emulator to within a few percent, before running.
- What the GS is sent today, from a GS dump of one frame (track02 lap frame
  1200, `tools/gs_dump.sh 1200` then `tools/gs_tools.py`; counts from the
  dump, not from timing):
  - About 930 polygons (1,385 triangles), 594,000 pixels drawn for a 307,200
    pixel screen, 110 KB, 251 texture binds across 46 textures.
  - 505 polygons (54%) are fully hidden by the end of the frame, but they
    are small: 18,000 pixels, 3% of those drawn. 73% of the pixels in
    hidden or tiny polygons passed the depth test when drawn: geometry
    arrives mostly back to front. Hidden polygons cost EE transform, clip
    and packet time, which is our bottleneck, more than GS fill, so
    visibility per track section (a precomputed list of what each section
    can see) may pay before VU1 does. Another 52 polygons drawn with
    ZMSK=1 (transparent) own no pixel but are on screen (345,000 pixels pass
    the depth test). Before ps-probe's fix for those, the replay called them
    hidden too, and gave 557 hidden polygons (60%) with 67% of the pixels.
    Approximate: the replay doesn't model the alpha test.
  - 16% of triangles are under a pixel; back faces drawn are negligible.
  - Every bind loads its palette (CLD=1). CLD=4 (load only when the palette
    address changes) would save none, because each texture has its own
    palette and every bind changes texture. Fewer palette loads means fewer
    binds: sorting opaque polygons by texture within a section.
- Batches end at matrix, texture and blend changes (see the architecture
  decisions), so the rewrite stays inside `render_ps2.c`.
- References (read, never copied): ps2gl's `vu1/` renderers, including clipping
  and culling; the VU coding contest demos (harness by SCEA/SCEE staff, entries
  from SCEA's contests); VCL or openvcl to schedule the microcode.
- Done when: a full race holds 60fps on real hardware with headroom to spare.
- If it doesn't, the last resort is a shorter draw distance (see stage 13's
  draw distance item): measured in PCSX2 at stage 10.2, cutting everything at
  wipeout-rewrite's 64000 saved 1.2–2.7 ms a lap, with no fade (it pops in).

### 13. Our enhancements
- **Music from the original CD audio.** The `.spu` music is converted from
  wipeout-rewrite's QOA tracks (`tools/convert_music.sh`), one lossy format into
  another (30–35 dB SNR against the QOA). Encoding from the user's own rip of
  the original CD audio would avoid that loss: the converter would take WAV.
  The user rips the discs; nothing is extracted here.
- **neGcon.** The original PS1 wipEout supported Namco's twist controller, and the
  PS2's BIOS pad driver identifies it (libpad's `PAD_TYPE_NEJICON`). Map the twist
  to steering and the analog I, II and L buttons to thrust, fire and airbrake, and
  find out what libpad reports for each axis. Test with a real neGcon.
- **Video options in the menu**, chosen at run time instead of `make VIDEO=`:
  - 480i and 576i (PAL 50Hz), 480p and 576p (component), 640×480 VGA.
  - 16:9 anamorphic for widescreen TVs (works already) next to 4:3.
  - gsKit also has VGA up to 1280×1024, and 720p and 1080i over component. None
    fits double-buffered 32-bit in 4 MB of VRAM: they'd need 16-bit buffers,
    half-height fields or a smaller rendered image, and the fill rate to match.
    800×600 VGA with 16-bit buffers is the likeliest of them.
- **Draw distance and fade.** wipeout-rewrite draws to 64000 and fades vertex
  alpha from 48000; the Dreamcast port cuts off hard at 64000–96000 per track
  section (up to 256000 for some scenery), so things pop in. A fade at the end
  of whatever distance is set (every GS primitive already blends, so it's a few
  instructions a vertex), with sections and objects culled by their nearest
  point instead of their centre, so big scenery fades in whole. Then the
  distance as a video option: the Dreamcast's tuned distances by default,
  shorter for speed. Screenshots (`tools/bench.sh shot N`) for each choice.
  In a frozen frame on track02 (lap frame 1080), 93 of the 554 polygons drawn
  cover less than a pixel.
  - PAL runs at 50fps; the physics already scale with frame time.

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
macro mode comes in stage 11, measured on hardware.

### How data reaches the GS: the GIF's three paths
The GS only takes data through the GIF, which has three inputs (Sony's EE
overview names them; when two are busy, PATH1 wins, then PATH2, then PATH3):
- **PATH1**: VU1 sends packets from its own memory with `XGKICK`. Stage 12.
- **PATH2**: VIF1 passes data straight to the GIF (`DIRECT`/`DIRECTHL`),
  without VU1's microprogram.
- **PATH3**: DMA channel 2 sends from main RAM to the GIF. Everything until
  stage 12: `gs_ps2.c`'s packets and texture uploads.
Commits and PRs that change how data reaches the GS name the path.

## Open questions
- Full-height or half-height rendering for 480i. Full-height (640x448, the GS's
  FIELD mode, FFMD=0) is what gsKit does by default and what we use; half-height
  (640x224 per field, FRAME mode) halves VRAM and fill rate but is softer.
- Whether 800×600 VGA with 16-bit framebuffers is worth the fill rate and VRAM
  (stage 13).
