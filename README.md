# NS2SNES-9X

**Native SNES emulation for Nintendo Switch 2**

A community development fork of [Rodpad/SW2SNES](https://github.com/Rodpad/SW2SNES), powered by Snes9x and designed for hbmenu under [kaiHEN](https://github.com/ouncebrew/kaihen).

![Development](https://img.shields.io/badge/development-v1.0.0--RC1-blue) ![CI](https://img.shields.io/badge/RC1%20CI-passed-brightgreen) ![Hardware](https://img.shields.io/badge/hardware%20validation-in%20progress-orange)

## Release status

**v1.0.0-RC1 — hardware validation in progress.**

The release candidate has passed host regression tests and ARM64 NRO compilation. 21 of 22 planned development phases have been implemented, with final hardware acceptance and public release remaining. The historical ounce-devkit 20261115 SDK is pinned following exit-crash testing.

[Releases](https://github.com/blueshockz47/NS2SNES-9X/releases) · [Report an issue](https://github.com/blueshockz47/NS2SNES-9X/issues)

The current release candidate is private. The public main branch retains the upstream baseline and project documentation until the stable release; the features below describe the RC1 development build, not the current public source. No stable download is available yet.

## What's new in NS2SNES-9X

### Interface and game library
- Edge-to-edge fullscreen grid and list layouts, with a 4 × 3 cover grid.
- Local PNG cover art, bounded cover cache and incremental loading.
- Search, favourites, recently played, playtime and sorting by name, playtime or recent activity.
- Multiple ROM folders, remembered selection and optimized library scanning.
- Dark, OLED Black, Light and SNES Classic themes with six accent choices.
- D-pad and analog-stick navigation in all four grid directions.

### Graphics and performance
- Switch 2 (16:9) fullscreen aspect option: fills the display by stretching, without cropping.
- ARM64 NEON RGB565 conversion and optimized rewind delta processing.
- Adaptive performance profiles, frame pacing and frame-delay refinements.
- PAL/NTSC regional timing, graphics upload validation and shader fallback.
- Retained OpenGL ES shaders, run-ahead, 120 Hz/VRR support and fast-forward.

### Audio and controls
- Adaptive audio queue recovery, buffer accounting and allocation cleanup.
- Independent turbo per player/button, neutral reconnect gating and opposing-direction filtering.
- Controller presets, per-player remapping and adjustable analog deadzone.

### Saves and compatibility
- Ten save-state slots with date/time, embedded integrity checks and previous-state backups.
- SRAM backups, safer temporary-file replacement and resume last game.
- Missing-only import of original SW2SNES SRAM and first four states for unpatched games.
- IPS, BPS and UPS sidecar patches, ZIP CRC validation and patch-specific save names.
- MSU-1 companion path support and enhancement-chip diagnostics.

### Stability and diagnostics
- Validated rewind delta decoding and reset after state loading.
- Per-session benchmark CSV logs with FPS, region-relative speed, timing, audio queue and rewind statistics.
- Historical SDK pinning, sanitizer-enabled host regression tests, synthetic NTSC/PAL/MSU-1 smoke tests and ARM64 build validation.
- Improved shutdown diagnostics, file-reading checks and SRAM bounds validation.

## Core features inherited from SW2SNES

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
- Save states per game, battery saves, optional auto-load of the latest state
- Two players, with separate button mapping and turbo for each
- Game Genie and Pro Action Replay codes (on-screen editor)
- Favourites, recently played and time played per game
- Zipped ROMs (`.zip` with a `.sfc`/`.smc` inside)
- Per-game or global settings; advanced options can be hidden

## Installation (for the upcoming stable release)

1. Set up kaiHEN and hbmenu on your Switch 2.
2. Copy the released `ns2snes-9x.nro` to `/switch/ns2snes-9x/ns2snes-9x.nro`.
3. Place your own SNES ROMs in `/roms/snes/` or select another ROM folder.
4. Launch NS2SNES-9X from hbmenu.

No ROMs are included. Back up saves before updating. Close the emulator before replacing its NRO. Settings, saves and logs are stored in `/switch/ns2snes-9x/`. Older builds cannot read the new wrapped save-state format; legacy compatibility is not guaranteed.

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

## Development roadmap

| Phases | Focus | Status |
| --- | --- | --- |
| 1–3 | Foundation, stability, diagnostics | Implemented |
| 4–7 | Core, adaptive performance, audio, input | Implemented |
| 8–11 | Library, themes, graphics, save manager | Implemented |
| 12–14 | Compatibility, content, beta hardening | Implemented |
| 15–17 | Fullscreen UI, library and save refinements | Implemented |
| 18–20 | Performance, controller and final beta fixes | Implemented |
| 21 | Release candidate | CI passed |
| 22 | v1.0.0-Stable | Pending hardware acceptance |

## Known limitations

Physical compatibility, long-session stability, power consumption and performance gains are still being evaluated. No measured speedup is claimed. Vulkan/HDR, multitap, named controller profiles and save-state thumbnails are deferred. Public source and stable binaries will be synchronized after acceptance.

## Credits

Original SW2SNES by Rodpad; Snes9x and its contributors; devkitPro, ouncebrew and the kaiHEN community. NS2SNES-9X development by blueshockz47.

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
