# NS2SNES-9X

**Native SNES emulation for Nintendo Switch 2 — development preview.**

NS2SNES-9X is a community development fork of [Rodpad/SW2SNES](https://github.com/Rodpad/SW2SNES). Our focus is stability, performance diagnostics, responsive controls and a refined user experience while preserving upstream credits.

## Project status

Phase 2 alpha has compiled successfully and passed host rewind sanitizer tests. Hardware validation is pending; pull request CI is still affected by an SDK download/cache issue. There is no stable NS2SNES-9X release yet.

[Download Phase 2 alpha (GitHub Actions ZIP)](https://github.com/blueshockz47/NS2SNES-9X/actions/runs/45475895332/artifacts/14959546610) · [Build history](https://github.com/blueshockz47/NS2SNES-9X/actions)

The public repository is intended for project information and future stable releases. A separate development repository is planned; its link will be added once created.

### Development roadmap

| Phase | Focus | Status |
| --- | --- | --- |
| 1 | Foundation and rebranding | Alpha built |
| 2 | Stability, rewind, audio and save safety | Alpha built; hardware/CI verification pending |
| 3 | Benchmark and diagnostics | Planned |
| 4–7 | Core performance, adaptive profiles, audio and input | Planned |
| 8–11 | Library, interface, graphics and save manager | Planned |
| 12–15 | Compatibility, content management, hardware testing and stable release | Planned |

## About

A Super Nintendo emulator for the Nintendo Switch 2, running as a homebrew app
from **hbmenu** under [kaiHEN](https://github.com/ouncebrew/kaihen). It uses the
**Snes9x** core with the [SW2NES](https://github.com/Rodpad/SW2NES) front end:
native C++ with OpenGL ES, built with devkitPro and the Switch 2 devkit
([ounce-devkit](https://github.com/ouncebrew/ounce-devkit)).

No game ROMs are included or provided.

## Installing

1. Set up kaiHEN on your Switch 2 so that hbmenu runs.
2. Copy `ns2snes-9x.nro` from the alpha download above to `/switch/ns2snes-9x/ns2snes-9x.nro`
   on the SD card.
3. Copy your games (`.sfc`, `.smc`, `.fig`, `.swc` or `.zip`) to `/roms/snes` on
   the SD card. NS2SNES-9X creates this folder on its first launch. A different
   folder can be chosen in Settings > ROM folder.
4. Launch **NS2SNES-9X** from hbmenu.

Settings, saves and play time are kept in `/switch/ns2snes-9x/` on the SD card.

### Updating

Replace `/switch/ns2snes-9x/ns2snes-9x.nro` with the one from a newer compatible build. Settings,
saves, save states and play time are kept in `/switch/ns2snes-9x/` and are not touched
by an update. (Close NS2SNES-9X on the console before uploading over FTP; a running
app's file cannot be replaced.)

## Inherited emulator features

- Snes9x (libretro), including its enhancement-chip support (SuperFX, SA-1, DSP-n, CX4, S-DD1, SPC7110 and others)
- Hi-res and interlaced modes (512 wide, 448/478 high)
- Low input latency: one frame in flight, run-ahead (1–2 frames), frame delay at 60 Hz
- 120 Hz / VRR on the Switch 2 screen
- Rewind (hold ZL) and fast-forward (hold or toggle ZR)
- GPU shaders: CRT-Royale (lite), CRT-Geom, CRT-Lottes, CRT-Easymode, CRT-Aperture,
  zfast CRT, LCD grid, Scale2x, sharp bilinear, bilinear
- NTSC signal simulation (composite, composite with dot crawl, S-Video)
- Curvature, rounded corners, vignette and brightness boost
- Aspect ratio, integer or fit scaling, overscan
- 4 save-state slots per game, battery saves, optional auto-load of the latest state
- Two players, with separate button mapping and turbo for each
- Game Genie and Pro Action Replay codes (on-screen editor)
- Favourites, recently played and time played per game
- Zipped ROMs (`.zip` with a `.sfc`/`.smc` inside)
- Per-game or global settings; advanced options can be hidden

## Controls

| Switch 2 | SNES / app |
| --- | --- |
| D-pad / left stick | D-pad |
| A, B, X, Y | SNES A, B, X, Y (default) |
| L, R | SNES L, R (default) |
| + / − | Start / Select |
| ZL + ZR | In-game menu (press again to close) |
| ZL (hold) | Rewind |
| ZR | Fast-forward |

Buttons can be remapped per player in Settings, including turbo.

In the game list: Y opens Settings, X refreshes the list, B exits, and L/R or
ZL/ZR switch between All, Favourites and Recent.

## Building

The instructions below describe the rebranded development branches; the public main branch still contains the upstream baseline.

Requirements: [devkitPro pacman](https://devkitpro.org/wiki/devkitPro_pacman) with
`switch-dev`, `switch-freetype` and `switch-zlib`, and the Switch 2 devkit
(`sh install.sh` from ounce-devkit).

```sh
export DEVKITPRO=/opt/devkitpro
make
```

This builds `ns2snes-9x.nro`. `tests/host_build.sh` builds a host (Mac/Linux) test of
the core bridge under AddressSanitizer.

## Layout

| Path | Contents |
| --- | --- |
| `source/` | The app: menus, settings, game loop, video (OpenGL ES), audio, input, rewind |
| `romfs/shaders/` | Picture shaders (GLSL ES) and the NTSC encoder/decoder |
| `native/bridge_snes9x.cpp` | C interface to Snes9x (frames, audio, states, cheats) |
| `third_party/snes9x` | Snes9x (from libretro/snes9x, see `SOURCE.txt`) |
| `tests/` | Host tests for the core bridge |
| `licenses/` | Licences of the bundled components (also inside the app) |

## Licence

The original SW2SNES code and NS2SNES-9X modifications (everything outside `third_party/`) is under the MIT licence
(see `LICENSE`). **Snes9x is not free software for commercial use**: its licence
allows free non-commercial use and distribution only, so NS2SNES-9X as a whole may
only be used and distributed non-commercially. See `licenses/snes9x-LICENSE.txt`.

It also includes:

- libretro-common (MIT)
- Roboto (Apache 2.0) and Source Code Pro (SIL OFL 1.1) fonts
- libounce and libnx (ISC), libkaihen (ISC), OunceVK / Mesa (MIT), mimalloc (MIT),
  FreeType (FTL), HarfBuzz (MIT), libpng, zlib, minizip (zlib licence) and bzip2

Their licence texts are in `licenses/`.
