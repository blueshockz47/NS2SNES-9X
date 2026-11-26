# NS2SNES-9X

**Native SNES emulation for Nintendo Switch 2**

A controller-friendly SNES emulator powered by Snes9x, designed for Nintendo Switch 2 homebrew under [kaiHEN](https://github.com/ouncebrew/kaihen). A development fork of [Rodpad/SW2SNES](https://github.com/Rodpad/SW2SNES).

![Release candidate](https://img.shields.io/badge/development-v1.0.0--RC12-blue) ![CI](https://img.shields.io/badge/RC12%20CI-passed-brightgreen) ![Hardware](https://img.shields.io/badge/hardware-accepted-brightgreen)

[Releases](https://github.com/blueshockz47/NS2SNES-9X/releases) · [Installation](#installation) · [Controls](#controls) · [Cover art](#cover-art) · [Report an issue](https://github.com/blueshockz47/NS2SNES-9X/issues)

> **First public beta coming soon.** RC12 passed host regression tests and ARM64 NRO compilation. The developer completed the handheld hardware acceptance checklist, including touchscreen navigation and game launch/exit. The public main branch currently retains the upstream baseline; the features documented here describe the private RC12 development build. No NS2SNES-9X beta binary has been published yet.

## What's new in the first beta

- **Touch-enabled library:** tap the search bar to open the native keyboard, clear searches, switch views, select categories and change pages. Tap a game to select it, double-tap to launch, swipe vertically in List or horizontally in Grid to change pages.
- **SNES Heritage interface:** italic wordmark, four-colour stripe, animated controller with live input feedback, aligned branding and navigation rows, and integrated search controls.
- **Smart Cover Matching:** dedicated cover folder, exact and case-insensitive filename matching, conservative title normalization for region and revision tags, and fallback to ROM-sidecar artwork. Ambiguous matches are skipped.
- **Modern game library:** fullscreen List and Grid, rapid page navigation, sorting, favourites, recent games, playtime and multiple ROM folders.
- **Refined emulation:** ARM64 NEON conversion, adaptive performance, improved audio recovery, rewind and frame pacing, ten save slots and safer save handling.

## Highlights

| Feature | What you get |
| --- | --- |
| Game library | Fullscreen List and 4 × 3 Grid, cover previews, search and sorting |
| Fast browsing | Tap or hold L/R to page through large collections |
| Native interface | Switch software keyboard, SNES Classic theme and fullscreen settings tabs |
| Emulation | Snes9x, enhancement chips, regional timing and run-ahead |
| Saves | Ten slots, timestamps, integrity checks, backups and resume |
| Offline | Local ROMs, local cover art and no required online service |

## Installation

Once the first public beta is available:

1. Set up kaiHEN and hbmenu on your Switch 2.
2. Download **ns2snes-9x.nro** from the [Releases page](https://github.com/blueshockz47/NS2SNES-9X/releases).
3. Copy it to the SD card as shown below.
4. Launch NS2SNES-9X from hbmenu. First-time setup creates the default ROM directory and configuration and displays initialization progress.
5. Copy your own SNES games into the ROM folder, then press X in the library to refresh.

```text
SD card/
├── switch/
│   └── ns2snes-9x/
│       ├── ns2snes-9x.nro
│       └── covers/
│           └── Super Mario World.png
└── roms/
    └── snes/
        ├── Super Mario World.sfc
        └── Super Mario World.png
```

Supported ROM extensions: `.sfc`, `.smc`, `.fig`, `.swc` and compatible `.zip` archives. Additional ROM directories can be configured in Settings. No ROMs or commercial artwork are provided.

### Updating

Close the emulator before replacing the NRO. Back up `/switch/ns2snes-9x/` first. Replace only the NRO; retain saves, settings and playtime files. Older builds cannot read the new wrapped save-state format.

## Controls

### Game library

| Button | Action |
| --- | --- |
| D-pad / left stick | Navigate |
| A | Launch selected game |
| L / R | Previous / next page (14 ROMs in List, 12 in Grid) |
| Hold L / R | Accelerated page scrolling |
| ZL / ZR | All / Favourites / Recent categories |
| + | Toggle List / Grid |
| − | Native ROM search keyboard |
| Y + − | Manual search fallback |
| ZL + − | Clear active search |
| X | Refresh library |
| Y | Open Settings |
| B | Back / exit confirmation |

### Touchscreen (handheld)

| Gesture | Action |
| --- | --- |
| Tap search field | Open native keyboard |
| Tap Clear Search | Reset query |
| Tap List / Grid or category | Switch view or category |
| Tap L / R page badge | Change page |
| Tap game | Select |
| Double-tap game | Launch |
| Swipe vertically in List | Previous / next page |
| Swipe horizontally in Grid | Previous / next page |

Touch support is currently limited to the library; Settings and in-game menus remain controller-operated.

### Settings

L/R switches between **Settings**, **Cover Art** and **Credits**. Up/down selects an option; left/right or A changes/selects it; B returns to the library.

### During gameplay

| Button | Action |
| --- | --- |
| D-pad / left stick | SNES D-pad |
| A/B/X/Y, L/R | SNES buttons (remappable) |
| + / − | Start / Select |
| ZL + ZR | Open or close in-game menu |
| Hold ZL | Rewind |
| ZR | Fast-forward (hold or toggle) |

A short on-screen reminder appears when starting a game. The C button is not used for the emulator menu.

## Cover art

No internet connection is needed. The recommended location is `/switch/ns2snes-9x/covers/`, created automatically. Use the ROM basename for the PNG, for example `Aladdin (U) [!].png`. Alternatively, put a PNG beside the ROM:

```text
/roms/snes/Aladdin (U) [!].sfc
/roms/snes/Aladdin (U) [!].png
```

For ZIP ROMs, match the ZIP filename, not the internal ROM filename. PNG dimensions must be at most 1024 × 1024. Press X to refresh after copying images. The emulator preserves artwork aspect ratio and caches images. See the in-app **Settings → Cover Art** tab.

The centralized folder supports exact, case-insensitive and normalized title matching (including common region/revision suffixes). A generic title can match a regional ROM when unambiguous. Exact filenames are recommended for regional variants. Checksum matching and online artwork retrieval are not included.

## Features

### Library and interface
- Edge-to-edge List View with selected-game cover preview and 12-cover Grid View.
- Native Switch software keyboard, manual fallback, favourites, recently played, playtime and sorting by name, favourites, recent activity or playtime.
- Multiple ROM folders, remembered selection, deduplicated scanning and bounded incremental PNG artwork loading.
- Visible page counters, L/R paging and accelerated hold-to-scroll; ZL/ZR category indicators.
- SNES Classic (factory default), Dark, OLED Black and Light themes, six accent choices.
- Fullscreen Settings, Cover Art guide and Credits tabs; first-launch setup feedback and directory error screen.

### Graphics and emulation
- Snes9x core with SuperFX, SA-1, DSP, CX4, S-DD1, SPC7110 and other enhancement-chip support.
- Hi-res/interlaced output, PAL/NTSC timing, run-ahead (1–2 frames), frame delay, rewind and fast-forward.
- ARM64 NEON RGB565 conversion, optimized rewind deltas, adaptive performance profiles and frame pacing refinements.
- OpenGL ES shader pipeline with upload validation and shader fallback; inherited 120 Hz/VRR support where available.
- CRT-Royale lite, CRT-Geom, CRT-Lottes, CRT-Easymode, CRT-Aperture, zfast CRT, LCD grid, Scale2x, sharp bilinear and bilinear.
- RGB/composite/S-Video simulation, curvature, corner radius/softness, vignette, brightness boost, overscan and screen borders.
- Switch 2 (16:9) fullscreen stretching option, original aspect options and integer/fit scaling.

### Audio and controllers
- Audio queue recovery, buffer accounting and allocation cleanup.
- Two-player remapping, controller presets, adjustable analog deadzone, independent turbo, neutral reconnect gating and opposing-direction filtering.
- Game Genie and Pro Action Replay cheats, global and per-game settings.

### Saves and compatibility
- Ten save-state slots with timestamps, integrity checks and previous-state backups.
- SRAM backups, safer temporary-file replacement, resume last game and optional automatic state loading.
- Missing-only import of original SW2SNES SRAM and first four save slots for unpatched games; original files remain untouched.
- IPS/BPS/UPS sidecar patches, ZIP CRC validation, patch-specific save identities and MSU-1 companion path support.

### Diagnostics and stability
- Validated rewind decoding and history reset after state loading.
- Per-session benchmark CSVs under `/switch/ns2snes-9x/benchmarks/`: FPS, region-relative speed, timing, audio queue and rewind statistics.
- Shutdown diagnostics, file-reading checks, SRAM bounds validation, sanitizer host tests and synthetic NTSC/PAL/MSU-1 smoke tests.
- Historical ounce-devkit 20261115 SDK pinned following hardware exit-crash investigation.

### Factory defaults

Fresh installations use SNES Classic theme, Balanced performance, one-frame run-ahead, FPS display, Switch 2 16:9, RGB, Scale2x and Medium brightness boost. Existing saved settings are retained.

## Troubleshooting and feedback

- **No games:** check ROM directory and extensions, then press X.
- **No cover:** verify the covers folder or sidecar path, basename, PNG extension and dimensions; refresh the library.
- **Menu:** press ZL + ZR together, not C.
- **Startup or exit issue:** note whether this is a fresh installation, and include the emulator log if available.
- **Performance issue:** include game/region, handheld or docked mode, settings and benchmark CSV if available.

[Open an issue](https://github.com/blueshockz47/NS2SNES-9X/issues/new) with version, reproduction steps, expected and actual behavior. Remove private information from logs before sharing.

## Roadmap

| Milestone | Focus |
| --- | --- |
| v1.0.0-RC12 | Hardware acceptance completed |
| v1.0.0-Beta | First public beta after acceptance |
| v1.0.0-Stable | Stable release after beta feedback |
| v1.1 | Expanded cover matching, metadata and premium library |
| v1.2 | Advanced save manager |
| v1.3 | Modern in-game menu |
| v1.4 | Performance and battery improvements |
| v1.5 | SwitchNest integration |
| v2.0 | Investigate advanced Switch 2 features |

Touchscreen Settings and in-game menu support are deferred until after the first beta. Future features are proposals, subject to SDK support and testing. No measured speedup or universal game compatibility is claimed.

## Credits

NS2SNES-9X development by **blueshockz47**. Original [SW2SNES](https://github.com/Rodpad/SW2SNES) by Rodpad; Snes9x and its contributors; devkitPro, ouncebrew, libounce and the kaiHEN community.

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
