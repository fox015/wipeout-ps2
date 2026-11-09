# wipEout for PlayStation 2

A native PlayStation 2 port of wipEout, built on Dominic Szablewski's
[wipeout-rewrite](https://github.com/phoboslab/wipeout-rewrite) by way of
jnmartin84's Dreamcast port, [wipeout-dc](https://github.com/jnmartin84/wipeout-dc),
which this repo is forked from.

## Status

Work in progress. In PCSX2 and ARMSX2 the whole game runs: menus, races,
textures and blending, sound effects, music with the PS1's tunnel reverb, and
saves on the memory card. It also runs on a real PS2 over ps2link. Next up:
loading the game data from the console's own drive, and booting without
ps2link.

The plan, stage by stage, is in [ROADMAP.md](ROADMAP.md). Which features come
from the PS1 game, the rewrite, the Dreamcast port and this port is in
[docs/FEATURES.md](docs/FEATURES.md).

## Game data

The game data isn't included. This port uses the data from phoboslab's
wipeout-rewrite download (see the end of
[Rewriting wipEout](https://phoboslab.org/log/2023/08/rewriting-wipeout)).
Unzip it into a `wipeout/` folder at the top of the repo (it's gitignored),
then convert the music to the PS2's sound format:

    tools/convert_music.sh

## Building

The toolchain runs in Docker (on a Mac, through Colima). The image is pinned
in `.devcontainer/Dockerfile`.

    docker build -t wipeout-ps2-dev .devcontainer
    docker run --rm -v $PWD:$PWD -w $PWD wipeout-ps2-dev make

That builds `wipeout.elf`. Build options:
- `VIDEO=vga|480p|480i`: display output (default `vga`, 640x480).
- `LOG=net`: send the log over ps2link instead of the serial port.
- `BENCH=1` / `BENCH=all`: a benchmark that races one lap by itself.

`make -f Makefile.dc` still builds the Dreamcast version (KOS v2.2.1).

## Running

- **In an emulator** (PCSX2 or ARMSX2 with HostFs on): open `wipeout.elf`. The
  game reads its data from `host:wipeout/`, next to the ELF.
  `tools/build_and_run.sh . "frames:"` builds, launches and follows the log.
- **On a PS2**: boot ps2link, then from the repo folder:

      ps2client -h <ps2 ip> execee host:wipeout.elf

## Controls

| PS2 pad | Action |
|---|---|
| D-pad / left stick | Steer, nose up and down |
| Cross | Thrust, select |
| Square | Fire |
| Triangle | Change view |
| L2 / R2 | Left and right airbrakes |

The controls can be remapped in the game's options.

## Credits

The original game is by Psygnosis (1995). wipeout-rewrite is by Dominic
Szablewski (phoboslab). The Dreamcast port is by jnmartin84 and contributors.
The PS2 port has been built with Claude Code using
[ps2sdk](https://github.com/ps2dev/ps2sdk) and
[gsKit](https://github.com/ps2dev/gsKit). The full list is in the game's
credits.
