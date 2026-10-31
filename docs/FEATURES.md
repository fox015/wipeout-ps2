# Features by origin

What this port does, grouped by where each feature comes from: the original PS1
game, phoboslab's wipeout-rewrite, the Dreamcast port this repo is forked from,
and the PS2 port. It's meant to grow into the plan for grouping the options menu
into "stock", "port enhancements" and our own additions.

How "stock" is known: the leaked source wipeout-rewrite was built from is mostly
the Windows "ATI Rage Edition", not the PS1 version. phoboslab rebuilt the menus
and the speedometer by watching the PS1 version in an emulator
([Rewriting wipEout](https://phoboslab.org/log/2023/08/rewriting-wipeout), 2023).
So "stock" below is the PS1 game as the rewrite reconstructs it, plus the Dreamcast
port's notes. None of it has been checked against the PS1 game itself.

## Stock PS1

| Feature | Notes |
|---|---|
| Race classes, teams, pilots, circuits; music and SFX volume; internal/external view | |
| Rapier class and the bonus circuit, earned by winning | The Dreamcast port also unlocks them from a menu (below) |
| Track vertex lighting, transparency | Lost in the PC ports, back in the rewrite |
| Boost and item pads at double brightness | The PS1 modulates texture × colour / 128; the GS does the same natively |
| Ship-to-track collision behaviour | The Dreamcast README says it restored the PS1 behaviour |

### Stock, restored

PS1 features missing from the leaked PC source, brought back by a later port.

| Feature | Status |
|---|---|
| Crowd cheers | Restored by the rewrite |
| PS1-style speedometer | Redrawn by the rewrite |
| Reverb in tunnels | Missing upstream. The PS2's SPU2 has hardware reverb: stage 7 |
| Intro video | The PS1 used STR video; the rewrite plays it as MPEG-1 (pl_mpeg). Not in this port; the PS2's IPU decodes MPEG in hardware |
| End-of-race animations | Missing from the leaked source and upstream |

## wipeout-rewrite

| Feature | Notes |
|---|---|
| Remappable controls, analog response curve | |
| Internal view roll, UI scale | |
| Screen resolution, CRT post-processing, fullscreen | The Dreamcast port replaced resolution with its display setting |
| Higher frame rates | Physics rewritten from a fixed 30fps step (25 on PAL) in fixed point to floats and a variable time step; may behave slightly differently from the PS1 |
| Z-buffered depth | The PS1 sorted polygons into depth slots (an ordering table), with occasional errors |
| Screen-shake slider, frame-time stats, "additional circuits" | Added upstream after the Dreamcast fork; not in this port |

## Dreamcast port (jnmartin84/wipeout-dc)

| Feature | Notes |
|---|---|
| Display: 4:3 or 16:9 anamorphic | Kept on PS2 |
| Texture filtering on/off, show FPS | Kept on PS2 |
| Bonus menu: unlock Rapier class and the bonus circuit | Kept on PS2 |
| Extended draw distance, 24-bit colour, screen shake reimplemented | |
| Slightly less green weapon colour | `weapon.c` |
| `fade`, `render_dist`, `shading` save fields | No menu entries; probably unused. Worth dropping when saves move to libmc (stage 8) |
| A Sonic image swapped onto one Venom Terramax texture (`sonic.raw`) | Not ported |
| 3D Dreamcast pad on the controls page (`dcpad.bin`) | Not ported |

## PS2 port

| Feature | Notes |
|---|---|
| 480i, 480p or VGA output | Build option (`make VIDEO=`), not a menu setting |
| Paletted textures | Invisible; fits a race in VRAM |

## Ours

Nothing yet.
