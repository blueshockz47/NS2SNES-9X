# NS2xSNES9x

**A modern SNES emulator for Nintendo Switch 2.**

Powered by Snes9x and designed for kaiHEN homebrew. Based on [SW2SNES by Rodpad](https://github.com/Rodpad/SW2SNES).

![Version](https://img.shields.io/badge/version-1.0.0--Stable-brightgreen) ![Platform](https://img.shields.io/badge/platform-Switch%202-lightgrey)

[**Download v1.0.0-Stable**](https://github.com/blueshockz47/NS2SNES-9X/releases/download/v1.0.0-Stable/NS2xSNES9x-v1.0.0-Stable.zip) · [Installation](#installation) · [Controls](#controls) · [Cover art](#cover-art) · [Report an issue](https://github.com/blueshockz47/NS2SNES-9X/issues)

## Features

- **Modern library:** fullscreen List and Grid views, search, favourites, recent games, playtime and multiple ROM folders.
- **Touch navigation:** native search keyboard, library gestures, Settings scrolling, folder selection and navigation buttons.
- **Smart Cover Matching:** PNG, JPG and JPEG support, automatic filename matching, custom folders, recursive scanning and unmatched artwork diagnostics.
- **Eight themes:** Dark, OLED Black, Light, Classic Violet, Retro Ivory, Neon Cyber, Retro CRT and Midnight Ocean.
- **Flexible emulation:** Snes9x core, enhancement chips, rewind, fast-forward, run-ahead, display filters and adjustable performance settings.
- **Save management:** ten save-state slots, SRAM backups, resume and per-game settings.
- **Offline operation:** no required internet connection.

## Installation

1. Download the latest release from [Releases](https://github.com/blueshockz47/NS2SNES-9X/releases).
2. Extract and copy the NRO to your SD card using either location below.
3. Add your own SNES games to the ROM folder.
4. Launch NS2xSNES9x through hbmenu under kaiHEN.

```text
SD card/
├── switch/NS2xSNES9x/
│   ├── NS2xSNES9x.nro
│   └── covers/
└── roms/snes/
```

**Alternative installation:** place `NS2xSNES9x.nro` directly in the SD card root (`/NS2xSNES9x.nro`) and launch it using a compatible homebrew launcher. The emulator still stores its data and cover art in `/switch/NS2xSNES9x/`.

Supported games: `.sfc`, `.smc`, `.fig`, `.swc` and compatible `.zip` archives.

**Updating:** close the emulator, back up its data folder and replace only the NRO. Keep your saves and settings. Older fork data may be imported automatically without overwriting existing files.

## Controls

| Button | Action |
| --- | --- |
| D-pad / left stick | Navigate |
| A | Play |
| L / R | Change page |
| ZL / ZR | Change library category |
| + | List / Grid |
| − | Search |
| X | Refresh |
| Y | Settings |
| B | Back / Exit |
| ZL + ZR (in-game) | Emulator menu |

In handheld mode, tap to select, double-tap a game to launch, or swipe to browse. Settings and library navigation also support touch. In-game menus remain controller-operated.

## Cover art

Copy PNG, JPG or JPEG images (up to 1024 × 1024) into `/switch/NS2xSNES9x/covers/`, then open **Settings → Cover Art → Scan Cover Art**.

Smart matching recognizes common naming conventions, for example:

| ROM | Cover |
| --- | --- |
| ActRaiser 2 (U).sfc | actraiser_2_usa.jpg |
| Aladdin (U) [!].sfc | Aladdin.jpg |

Custom folders and subfolders are supported. Original files are never renamed. Artwork retains its aspect ratio in both library views. Some titles may require exact filenames; online artwork downloads are not included.

## Help & feedback

If games or artwork are missing, check your folder settings and refresh or rescan. For crashes or compatibility issues, include your emulator version, game, settings and reproduction steps in a [GitHub issue](https://github.com/blueshockz47/NS2SNES-9X/issues).

## Credits

Developed by **blueshockz47**. Based on [Rodpad's SW2SNES](https://github.com/Rodpad/SW2SNES), with thanks to Snes9x contributors, devkitPro, ouncebrew, libounce and the kaiHEN community.

No ROMs or commercial artwork are included.

## Licence

The original SW2SNES code and NS2xSNES9x modifications (everything outside `third_party/`) is under the MIT licence
(see `LICENSE`). **Snes9x is not free software for commercial use**: its licence
allows free non-commercial use and distribution only, so NS2xSNES9x as a whole may
only be used and distributed non-commercially. See `licenses/snes9x-LICENSE.txt`.

It also includes:

- libretro-common (MIT)
- Roboto (Apache 2.0) and Source Code Pro (SIL OFL 1.1) fonts
- libounce and libnx (ISC), libkaihen (ISC), OunceVK / Mesa (MIT), mimalloc (MIT),
  FreeType (FTL), HarfBuzz (MIT), libpng, libjpeg-turbo, zlib, minizip (zlib licence) and bzip2

Their licence texts are in `licenses/`.
