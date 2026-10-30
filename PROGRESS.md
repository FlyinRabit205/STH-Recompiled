# Shadow the Hedgehog (Xbox, USA): Native PC Port - Progress Log

Native PC port by **static recompilation**: the game's original x86 code is translated to C++ ahead of time and
compiled into a normal Windows program. No emulator. The Xbox-only parts (kernel, Direct3D 8, DirectSound, controller)
are replaced by native Windows code (Direct3D 12 renderer, XAudio2 audio, SDL3 controllers).

Built on the open-source **XboxXBERecomp** SDK (MIT, by TGP482): https://github.com/TGP482/XboxXBERecomp
A copy of it lives in `sdk/` (with one small local patch, see below).

**Game files are never part of this project.** They come from the user's own copy at build time.

---

## Work in 1-hour sessions

Each session ends by saving to this folder on the user's PC:
`C:\Users\Hashir\Downloads\Shadow the Hedgehog (USA) (En,Ja,Fr,De,Es,It)\ShadowNativePort\`

- `PROGRESS.md`: this file. **Read first at the start of every session.**
- `ShadowNativePort-session<N>.tar.gz`: the whole project (source, config, hooks, tools, notes). Generated code is
  left out because it is rebuilt from the game in about 3 seconds.

### Resume checklist for Claude (start of a session)
1. Stage `PROGRESS.md` and the newest `ShadowNativePort-session*.tar.gz` from the folder above, then extract to `/home/claude/`.
2. Stage the game: the 7 split-zip parts `Shadow the Hedgehog Rom.xiso.z01..z06` + `.zip` from the game folder
   (one file per stage call, about 300 MB each). Rebuild the ISO with
   `python3 -I tools/host/unsplit.py part.z01 ... part.zip shadow.iso` (expected CRC32 `4fe176ca`, 2,538,209,280 bytes).
   - The `.xiso.iso` files (2.5 GB) are over the 400 MB stage limit, so use the split zip.
3. Extract: `python3 -I tools/host/xiso.py extract shadow.iso /home/claude/rungame` (all files, for the harness),
   and copy `default.xbe` to `assets/default.xbe`.
4. Build tools (Linux): `cd tools/host/toolbuild && CC=clang CXX=clang++ cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release . && ninja -C build`
   (`toolbuild/CMakeLists.txt` points `SDK` at `/home/claude/ShadowNativePort/sdk`).
5. Recompile: `xbe_recomp config/game.toml --xdk sdk/xdk` (about 3 s).
6. Headless build: `JOBS=2 bash sdk/harness/build.sh -r . -g generated/game -o /home/claude/hb -n shadow -x texturedumper`
   (about 3 min clean, seconds incremental).
7. Run: `timeout 40 /home/claude/hb/shadow /home/claude/rungame` or
   `REF=400 bash sdk/harness/run.sh -x /home/claude/hb/shadow -g /home/claude/rungame -o /home/claude/run -t 50`
   (reference frames land in `<run>/1/cap/ref_*.tga`).

---

## Game facts

| | |
|---|---|
| Title / ID | Shadow The Hedgehog, `53450086`, retail |
| XDK | 5849 (all 8 libraries: XAPILIB D3D8 DSOUND XBOXKRNL LIBCPMT LIBCMT D3DX8 XGRAPHC) |
| Entry / kernel thunk | `002AFB53` / `00429AE0` |
| Code | `.text` 3.9 MB; sections D3D, DSOUND, PSFD00 (CRI Sofdec), D3DX, XGRPH, XPP, DOLBY |
| Kernel imports | 111, all resolved by the runtime |
| Engine / formats | RenderWare-based (`.one` archives, `.txd` textures, `.dff`), CRI ADX audio (`.adx`, `.afs`), CRI Sofdec movies (`.sfd`) |
| default.xbe SHA-1 | `7e63ee47ec4e37c3104d4f5fec9bde37e9cf1aa3` |

---

## Status

### Session 1 (2026-10-30, 22:28 to 23:15): game boots and renders frames headless
- Rebuilt and verified the ISO; wrote XISO and XBE readers (`tools/host/`).
- Built `xbe_recomp` + `xbe_symbols` on Linux. `xbe_symbols` matched **135/243** XDK functions and 8/12 data.
  Most misses are libraries Shadow does not use (Bink, XNet/Winsock).
- Recompiled: **39,446 functions**, 3.98 MB of 4.30 MB code bytes, 510 jump tables, C++ EH handled.
  "unimplemented aaa/daa/bound/..." in the recompile output are data misread as code (gap fill); ignore unless something traps there.
- Headless harness builds (about 3 min on 2 cores).
- Boot fixes:
  1. `[functions] 0x37FC20`: indirect call target the analysis missed.
  2. `src/game/hooks.cpp`: host versions of 3 DSOUND exports the signatures did not name (they hit APU hardware and segfaulted):
     `003E577A` IDirectSound_SetAllParameters (stub), `003E4932` IDirectSoundBuffer_SetConeAngles (stub),
     `003E4148` IDirectSoundBuffer_SetPitch (converted to SetFrequency). Names inferred from .XTLID id ordering; arg counts verified by `ret N`.
- **Result:** the game runs 40+ s with no crash, about 24 fps headless, thousands of draws. It reads nukkoro.inf, loads DiscErrorEN.txd,
  the sound effects image, and vertex shaders.
- **But:** reference-rendered frames 400/800/1200 are **all black**.

### Local SDK patch
- `sdk/harness/compat/windows.h`: added `MessageBoxW` + `MB_*` (newer runtime d3d8.cpp uses them; the harness shim lagged).
  The harness is also built with `-x texturedumper` (Windows-only WIC code).

---

## Next session (2): start here
1. **Black frames.** Log line `d3d: MakeSpace from 003D8BC9, some caller still pushes hardware commands`: some caller
   writes raw NV2A push-buffer commands. Missing symbols from `xbe_symbols` that fit: `D3DDevice_BeginPush`,
   `D3DDevice_EndPush`, `D3DDevice_SetVertexShaderConstant1/4`, `D3DDevice_SetIndices`, `D3DDevice_SwitchTexture`,
   `D3DDevice_SetRenderTargetFast`, `D3DDevice_InsertFence/BlockOnFence`, `D3DDevice_SetPalette`. Find who calls
   MakeSpace (`003D8BC9` caller) and map these by hand (Ghidra via `python sdk/tools/symbols/xbe2pe.py config/game.toml game.exe`,
   or objdump). Fill `[symbols]` in `config/game.toml`.
   - Also check whether the game is just in an intro movie (Sofdec `.sfd`) or a fade from black: use `CAP=60` draw lists and longer runs.
2. **Music/streams.** `DirectSoundCreateStream` + all `CDirectSoundStream_*` are 0 in config. CRI ADX probably streams through them. Locate and map.
3. `crt_atan`, `crt_log`, `crt_log10`, `crt_tan` are 0: map them (CRT math fixes) once found.
4. Script pad input (`SCRIPT=`) to press START through menus once frames show.
5. Then the first **Windows build** on the user's PC (see below).

## User's PC setup (needed later, for the real Windows build)
- Visual Studio 2022 Community with **"Desktop development with C++"** + **"C++ Clang Compiler for Windows"**
  (`build.bat` offers to add the Clang parts itself).
- Put `default.xbe` and the game folders (or the .iso) into `ShadowNativePort\assets\`, then run `build.bat`.

## Goals the user asked for (keep in mind)
- **Fully native** (no emulation): yes, by design.
- **Best-quality PC port:** resolution options (1440p, 4K and beyond, native internal resolution, not upscaled),
  graphics settings menu (resolution, window mode, V-Sync, AA, texture filtering), proper widescreen (ultrawide as a stretch goal),
  solid **60 FPS**, and **above 60 FPS** later (needs game-logic timing decoupled or interpolated; do after it is playable).
- **Efficiency:** keep sessions lean; heavy Windows builds run on the user's PC between sessions; test only what changed.
- Order: boot, then playable, then PC polish.
