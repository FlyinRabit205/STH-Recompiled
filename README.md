# STH Recompiled - Shadow the Hedgehog Native PC Port

An unofficial native PC port of Shadow the Hedgehog (Xbox, USA), made by static recompilation: the game's x86 code is
translated to C++ and runs directly on the PC, with no emulator. Built with the
[Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) SDK.

> [!NOTE]
> This project contains no game assets and no game code. You need your own copy of the game (a disc image made from
> your USA Xbox disc). The release builds the game code from your disc on its first start.

## Install (players)

Download a zip from the **Releases** page.

- **Windows:** extract the zip, then run `Setup.exe`. Choose an install folder, your `.iso` (or the folder holding
  `default.xbe`), and optionally the HD texture pack zip.
- **Linux / Steam Deck:** extract the zip, then run `./install.sh <your .iso> [texture pack .zip]`. Play through
  Proton (add `Shadow.exe` as a non-Steam game) or Wine 9+. See `README-Linux.txt`.

The first start builds the game code from your disc (a few minutes, once). The install folder is self-contained,
so you can move or rename it.

### HD textures (optional)

The port can use the fan-made "Shadow the Hedgehog HD" Dolphin texture pack, which is not included. Drop its zip into
`textures\pack` (or pick it in Setup). The next start converts it once into `textures\replacement\pack_import.pak`.
`textures\pack_map.txt` maps each Xbox texture to its pack image. The map is checked at full size against the
originals, which removes false matches such as dark effect textures.

## Features

- Runs at 60 FPS, with in-engine cutscenes at 60 FPS (option), and cutscene skipping.
- Any resolution, borderless/fullscreen/windowed, V-Sync, render scale, anisotropic filtering, and an HD scene
  target.
- HD font sheets for English text and subtitles.
- Xbox, PlayStation and Switch controllers with hot plugging and rumble, plus keyboard.
- An in-game settings menu, including quit to desktop.
- Frame capture (F3) for bug reports.

## Building

- Windows cross-build from Linux: `tools/winbuild/README.md`, then `RELEASE=1 tools/winbuild/build_win.sh exe`.
- Release zips: `tools/release/make_release.sh <version>`.
- Your own `assets/default.xbe` is needed to generate the code (`generated/` is never committed).

## Layout

| Path | What |
| --- | --- |
| `config/game.toml` | recompiler config: symbols, hooks |
| `src/game/hooks.cpp` | game-specific hooks (frame pacing, cutscenes, CPU lock) |
| `src/common/` | settings menu, renderer extension, texture pack import |
| `tools/installer/` | Setup.exe source |
| `tools/release/` | release packaging, Linux scripts, texture pack map |
| `tools/host/` | texture pack matching tools |
| `sdk/` | Xbox XBE Recomp SDK (MIT) |

Shadow the Hedgehog is a trademark of SEGA. This project is not affiliated with SEGA.
