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
7. Windows build: set up per `tools/winbuild/README.md`, then `JOBS=2 tools/winbuild/build_win.sh exe dll`
   and send `Shadow.exe` + `Shadow.dll` to `ShadowNativePort\Game\` on the user's PC (device_commit_files).
8. Run: `timeout 40 /home/claude/hb/shadow /home/claude/rungame` or
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

### Session 1 (2026-10-30, 22:28 to 23:00): game boots and renders frames headless
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

### Session 2 (2026-10-30, 23:02 to 23:22): Windows build made here; intro logos render
- **Windows cross-build working on Linux** (user does NOT need Visual Studio): `tools/winbuild/` (see its README).
  `Shadow.exe` = runtime via llvm-mingw; `Shadow.dll` = game code via clang MSVC target, freestanding.
  `host_def.py` maps the 6 C++ runtime entry points (MSVC mangling to Itanium) in the DLL's import lib.
  Patch: `XBE_PREBUILT_GAME_CODE` in `sdk/runtime/app/game_code.cpp` skips the on-PC compile.
- **Fixed: `D3DDevice_BeginPush = 0x3D4740`, `D3DDevice_EndPush = 0x3D4760`** (mapped by hand in config). Found via gdb
  breakpoint on `__imp__sub_003D8860` (texture-stage program writer); the game's raw push-buffer draws now reach
  the renderer (+27% draws, no more MakeSpace warning).
- **Headless frames now show the SEGA / Sonic Team / Dolby Digital logos with fades** (`notes/session2_logos.png`).
- Boot sequence seen in the log: sound banks, character/shadow.one, fonts, Logo.xncp, then **`Op.sfd` (opening movie) at 1.1 s**.
- **Movie still black headless.** Findings:
  - Per movie frame the game calls GetSurfaceLevel2 (`003DD7C0`), AddRef (`003D5ED0`), Release x2 (`003D5F10`).
    `003DD810` is LockRect (5 args). None of these are mapped yet (original code runs on guest objects; seems OK).
  - The game renders into its own render targets (surface objects 0077E5D0 / 00A285B0 / ...), all at memory **03A2B080**,
    then draws that as texture `006E7F10` (512x512 X8R8G8B8) onto the back buffer, plus texture `00A0CFD0` (1024x512 A8R8G8B8
    at 03228080, alpha-blended, **has data**: the decoder works).
  - 03A2B080 reads all zero. Likely a **harness limitation** (the reference renderer only draws captured frames, so
    render-to-texture from other frames never lands in memory). **Test on a real GPU before chasing further.**
  - Physical addresses read through the 0x8xxxxxxx view (runtime design), not 0x0xxxxxxx.
- Unmapped DSOUND .XTLID exports are not called during the movie (stream functions are not the problem).

### Local SDK patches (keep when updating the SDK)
- `sdk/harness/compat/windows.h`: `MessageBoxW` + `MB_*` added.
- `sdk/runtime/app/game_code.cpp`: `XBE_PREBUILT_GAME_CODE` early return in `StaleTargets`.
- `sdk/harness/reference_renderer.cpp`: P8 textures decoded with the stage palette (`FindImage` takes the palette).
- Harness built with `-x texturedumper`.

---

### Session 3 (2026-10-30, 23:26 to about 23:50): keyboard controls, title screen fixed
- User tested session 2's build on Windows 11 (i7-6820HQ, Quadro M5000M): **ran clean, opening movie plays**
  (the black movie was only the headless reference renderer). Render scale 2.25 at 1080p worked.
  User reported: title screen "a bit broken", no keyboard control.
- **Keyboard controls**: `src/common/keyboard.{h,cpp}`, hooked as `GameInfo::window` + `GameInfo::pad`.
  Merges into port 1 with a real controller; keeps port 1 connected even unfocused (no "controller removed" pause);
  reads keys only while the game window has focus. Option `keyboard` in settings.toml. Layout in `CONTROLS.txt`.
- **Title screen fix: `D3DDevice_SetPalette = 0x3D0E80`** (mapped by hand). Title art is P8 (palettized, fmt 0x0B);
  without SetPalette the renderer had no palettes and drew grey noise. Found by searching D3D code for the NV2A
  texture palette method 0x1B20 (0x41B20 push header); CDevice_Palettes 0xF98 matches the SDK layout.
- Harness reference renderer patched to decode P8 with the stage palette (it passed nullptr before).
- Verified headless: title screen, autosave prompt, **Story Mode menu (NEW/CONTINUE/LIBRARY)** render correctly,
  scripted START presses navigate the menus (`notes/session3_title.png`).
- build_win.sh now rebuilds exe objects when any header changes (image_symbols.h carries hand-mapped addresses;
  without this the exe silently missed SetPalette).
- Skip-movie pad script: `120.. START` every 50 frames (`SCRIPT=<file>`, see harness README).

### User test after session 3 (2026-10-30, 23:50)
- **Works:** title screen (much better), movies play, graphics load, **first stage (Westopolis) loads and runs.**
- **Problems reported:**
  - Text is badly garbled (title screen and elsewhere), subtitles too.
  - Cutscenes (in-engine) need work: rendering issues.
  - **In-stage picture is very low resolution** (even though the log shows render scale 2.25 at 1080p, so some
    render targets / passes are probably still at 640x480 and stretched; check render-to-texture targets the game makes
    itself, and copies/blits back to the back buffer).

### Session 4 (2026-10-31, 14:24 to about 15:00): cutscene skip, HD scene
- **Cutscenes skippable on first playthrough** (setting `skip_cutscenes`, default true), via two `[[hook]]`s in
  config/game.toml calling `ShadowMovieSkippable` / `ShadowEventSkippable` (src/game/hooks.cpp):
  - Pre-rendered movies (StageMovie, vtable 0046478C): update method 00171770, state 1 reads "unskippable" byte
    `[esi+0x18]` at 001717DF (constructor sets 1); START test is `pad[0x148] & 0x400` at 001717FF (pad singleton
    at [0x5356F0], made by 000113B0). Hook zeroes AL. **Verified headless: E1001.sfd skipped in ~1 s, new game
    reaches Westopolis (stg0100) in 26 s.**
  - In-engine events: after START at 00228776, `mov al,[0x54DE01]` at 0022879B is the global "skip allowed";
    hook forces AL=1 (sets event this+0x34). Same pattern, **not yet seen running** (no event at Westopolis start).
  - Opening movie Op.sfd (AdvReal Movie phase, START test at 0029CEDD) was already skippable.
  - Found via RTTI (`tools/host/rtti.py <class substring>` prints vtables) + byte search for
    `mov r32,[reg+0x148]; test $0x4,highbyte`.
- **Low resolution in stages: cause found and fixed.** The game renders the 3D scene into a **512x512** colour
  target (+512x512 depth), then stretches it over the 640x480 back buffer (logged surfaces: rt 03A2B080 / 032F6080
  512x512, zs 03B2C000 512x512, glow chain 256/128/64). At render scale 2.25 that is 1152 px across 1440 screen px.
  `src/common/render_ext.cpp` = a `gpu::Extension` (set in `GameInfo::init`) whose `MakeTarget` multiplies the scale
  of 512x512 targets by 640/512 (1440x1440 at 1080p, 2880x2880 at 4K). Setting `hd_scene` (default true). Logs
  "shadow: HD scene ..." once. **Needs the user's eyes on Windows.**
- Windows build: `sdk/runtime/app/game_code.cpp` skips `hook_exports.h` under mingw (MSVC /EXPORT names break the
  mingw link); host_def.py maps the DLL's hook imports (now 8 C++ imports).
- User feedback this session: don't spend time re-rendering what the user already described; work from their report.

### Local SDK patches (add to the list above)
- `sdk/runtime/app/game_code.cpp`: `#ifndef __MINGW32__` around `#include "hook_exports.h"`.

## Plan for the next sessions (set by the user)
Done: cutscene skip (session 4), stage resolution (session 4, to confirm).
Remaining, in order:
1. **Confirm on Windows**: stages sharp now? cutscene skip works for movies AND in-engine events? Read `Game\logs`.
2. **Fix garbled text and subtitles** (title screen text, loading screen, subtitles). Fonts: `/fonts/*/X_EN.txd/.met/.fnt`,
   `Movie_EN` for subtitles. Check font texture formats and how glyphs are drawn (CAP draw lists in the harness).
3. **Cutscene rendering issues** (in-engine events).
4. **HD HUD / GUI / subtitles** (2D at native resolution, widescreen placement).
5. Later: settings menu, widescreen, 60 FPS+.

### Still open
- Intermittent segfault in the Linux harness (about 1 in 4 runs, timing dependent, not under gdb; core_pattern is
  `core`, run with `ulimit -c unlimited` to catch one). Never seen on Windows so far.
- Unmapped from xbe_symbols: SetVertexShaderConstant1/4, SetIndices, SwitchTexture, SetRenderTargetFast,
  InsertFence/BlockOnFence, GetTile/SetTile, PersistDisplay, FlushVertexCache, D3DResource_IsBusy,
  crt_atan/log/log10/tan. Unmapped D3D .XTLID exports seen running: GetSurfaceLevel2 `003DD7C0`,
  LockRect `003DD810`, AddRef `003D5ED0`, Release `003D5F10`.
- User's log showed `d3d: EndPush primitive 8 dropped, 64 bytes of vertices of 28` once at boot, and many
  `vertex: converted on the CPU: v1 type 24` in the stage (performance only).

### Handy techniques (worked well)
- **Which unnamed export runs:** dump `.XTLID` (id>>16: 8 = D3D, 0xB = DSOUND), then gdb `dprintf sub_<addr>,"HIT ..."` on each
  unmapped address and run (write gdb output to a file; use `timeout -s INT`).
- **Who reaches a function:** `gdb -batch -ex 'break __imp__sub_XXXXXXXX' -ex run -ex 'bt 14'` on the harness exe.
- **Direct callers:** `python3 -I tools/host/callers.py <hexaddr>...` (scans E8/E9 rel32).
- **Disassemble guest code:** `objdump -D -b binary -m i386 --adjust-vma=$((SECTION_VA - SECTION_RAW)) --start-address=... default.xbe`.

## Running the Windows build on the user's PC
- Files: `ShadowNativePort\Game\Shadow.exe` + `Shadow.dll`.
- Game: a folder named `assets` beside the exe (or up to 6 folders above) holding the game `.iso`.
  Plan: `ShadowNativePort\Game\assets\Shadow the Hedgehog (USA) (En,Ja,Fr,De,Es,It).xiso.iso` (moved, not copied).
- Needs Windows 10/11, a D3D12 GPU, VC++ 2015-2022 x64 redistributable. Logs go to `Game\logs\`.

## Goals the user asked for (keep in mind)
- **Fully native** (no emulation): yes, by design.
- **Best-quality PC port:** resolution options (1440p, 4K and beyond, native internal resolution, not upscaled),
  graphics settings menu (resolution, window mode, V-Sync, AA, texture filtering), proper widescreen (ultrawide as a stretch goal),
  solid **60 FPS**, and **above 60 FPS** later (needs game-logic timing decoupled or interpolated; do after it is playable).
- **Efficiency:** keep sessions lean; test only what changed. **The user asked Claude to do all building** (cross-build here, ship exe+dll).
- Order: boot, then playable, then PC polish.
