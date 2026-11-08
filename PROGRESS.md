# Shadow the Hedgehog (Xbox, USA): Native PC Port - Progress Log

Native PC port by **static recompilation**: the game's original x86 code is translated to C++ ahead of time and
compiled into a normal Windows program. The Xbox-only parts (kernel, Direct3D 8, DirectSound, controller)
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

### Session 4 (2026-10-31, 14:24 to about 15:00): cutscene skip, HD scene, widescreen scale, settings menu
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

- **Widescreen sharpness** (user: "should be 1920x1080 at 1080p"): the game is told widescreen (EEPROM video flags) and
  its 640x480 frame is shown 16:9, but the render scale came from the height only (2.25 = 1440 px across, stretched
  to 1920). `sdk/runtime/app/settings.cpp` RenderScale now takes max(height scale, width / 640) when widescreen:
  3.0 at 1080p (1920x1440 back buffer, supersampled to 1080 tall), scene target 512*3*1.25 = 1920.
- **Settings menu (F1)**: `src/common/settings_menu.cpp` (ImGui via GameInfo::ui/message/window). Display mode,
  resolution (monitor modes, or desktop), V-Sync, frame rate 30/60, render resolution 50-200%, texture filtering,
  HD 3D world, skip cutscenes, keyboard, FPS counter. Saves at once; display changes via app::ApplyDisplay; renderer
  re-reads the scale every present; render_ext Settings() remakes targets when HD scene toggles. Game input is
  blanked while the menu is open (GameInfo::pad). F1 only toggles it (Escape would leak START to the game).
  **Untested on Windows yet.**

- **Garbled text FIXED** (user screenshots: "There is no Game Data..." drawn as "oNoNoNo", subtitles "e!e!e!").
  Cause: text is drawn one glyph per draw (DrawIndexedVertices, 4 verts / 6 indices, FVF 0x142, font texture
  00BE8C60), from a vertex buffer the game rewrites before every glyph without waiting. With the render thread on
  (Windows), vertex memory is read when the draw runs, by then holding the last glyphs written. The Linux harness runs
  without the render thread (XBE_RENDER_THREAD=0), which is why it drew text correctly.
  Fix in `sdk/runtime/xdk/d3d8.cpp`: `DrawSnapshot` / `DrawIndexedSnapshot` copy small draws' vertices (<= 4 KB,
  stream 0 only, indexed <= 256 indices, gathered in index order) at call time through DrawUp's vertex ring.
  `SHADOW_NO_SNAPSHOT=1` turns it off for testing. **Verified** with a render-thread harness build
  (`sdk/harness/build_rt.sh`, a copy of build.sh with XBE_RENDER_THREAD=1, `-G /home/claude/hb/gen` shares objects):
  bug reproduced with the fix off, dialog text correct with it on.
- **Options menu redesigned** (user: "bland", wants an actual game menu): full-screen dim + red glow, OPTIONS title
  band, tabs DISPLAY / GRAPHICS / GAME / CONTROLS, rows with < value > selectors, description line, button hints,
  Bahnschrift font from C:\Windows\Fonts (Segoe UI fallback) loaded in GameInfo::window before the atlas is built.
  Navigation: arrows/WASD, Left/Right change, Q/E or Tab switch tabs, Enter/Space, mouse; controller: Back+Start opens,
  d-pad/stick/A, LT/RT or White/Black switch tabs, Start closes (`MenuPad` reads the real pad before the game's
  input is blanked). **Not seen on screen yet** (no GPU here).
- **Blur**: user says graphics still blurry although the log shows render scale 3.0 and scene 1920x1920.
  Not investigated yet: suspect the game's glow/blur chain (targets 256/128/64 composited over the scene) or the
  final scene-to-back-buffer pass. Next session.
- **Harness crash identified** (not the text fix): SIGSEGV in game code `sub_0034AB40` <- 003491B0 <- 00349280 <-
  0032D430 <- 0032D500, on game worker thread started at 00328E80 (middleware region, likely CRI audio/movie).
  Happens with pad scripts / REF captures under harness timing, with and without the render thread. Not seen on
  Windows. Investigate if the user ever crashes.

### Session 5 (2026-10-31, 15:13 to about 15:30): the blur
- User video: blur at every resolution; polygon edges soft while HUD sharp (so not textures, not resolution).
- In-stage frame (harness CAP draw list, `tostage.txt` = START every 50 frames to 1150, stage at ~frame 1260):
  136 draws scene -> 03A2B080 (512, scale 3.75 = 1920); glow objects -> 032F6080; blur chain 128/64 (03400280,
  03414380, 03410300, 03424400); glow 00BDF5A0 added ONE/ONE onto the scene; **scene texture 006E7F10 (fmt 7, data
  03A2B080) drawn to the back buffer**; then HUD.
- **Cause:** `BindTargetTexture` (sdk/runtime/gpu/d3d12.cpp) marks a sampled target "wide" (glow box taps, 3 console
  texels each, `kGlowBoxTexels`) whenever the target is larger than the destination in ANY guest dimension. The scene
  (512 tall) drawn to the back buffer (480 tall) qualified, so every frame was box-averaged over ~11 host pixels.
- **Fix:** wide only when the source is >= 1.5x the destination in a dimension (`kShrinkMin`). Glow chain steps (2x, 4x)
  keep their look. **Needs the user's eyes.**

### Session 6 (2026-10-31, 15:21 to about 15:50): stutter
- User: sharp at 1080p now. Dips to ~25 fps for a moment on the opening ramp of Westopolis, otherwise 60.
- Cause (by code reading): every GPU shader was compiled from HLSL with D3DCompile on the render thread the first
  time it was needed, and pipelines made synchronously; both cached only in memory, so every launch recompiled.
- Fixes in `sdk/runtime/gpu/d3d12.cpp`:
  - `cache\shaders.bin` beside the exe: compiled DXBC by source hash (ShaderDiskCache), loaded on first use.
  - `cache\pipelines.bin`: ID3D12PipelineLibrary (driver specific, rebuilt if the driver changes), saved 5 s after
    new pipelines. Pipelines named by key + shader bytecode hashes.
  - AsyncCompiler: draw shaders compile on 2-4 worker threads; a draw whose shader is not built yet is skipped
    (new effect a frame or two late on its first ever appearance) instead of stalling the frame.
  - Stutter log: frames over 22 ms are logged ("hitch: frame N took X ms: shaders, pipelines, texture uploads"),
    first 300, from frame 120. **Ask the user for the log if any dip remains** to see if it is shaders, pipelines,
    texture uploads, or none of them (then game/CPU/disc side).

### User test after session 6: no stutter in two runs. User asks: keep re-optimizing after every change.

### Session 7 (2026-10-31, 15:33 to about 16:10): HD environment textures from Cyb3rtron's GameCube pack
- Pack: `shadow_the_hedgehog_hd.zip` (user put it in ShadowNativePort\), 3235 GameCube-pack DDS (DXT5, 6x, 1 mip), folders
  GUP/Stages (23 stages), Backgrounds, Crates, Characters, Effects, HUD, Menu, Text, ... User's split: **environment
  from the pack; fonts/text redone by Claude in HD; GameCube buttons replaced by Xbox buttons made by Claude.**
- Xbox side, offline from the disc: `tools/host/one_extract.c` (Sega PRS "One Ver 0.60" archives: count at 0x1C,
  0x38 byte entries from 0xB0: name[0x2C], unpacked size, offset from 0x0C), `xbtex.py` (RenderWare Xbox texture
  natives, platform 5; NV2A formats; Shadow's palettized textures are 8 bpp with 32-colour (raster 0x4600) or
  256-colour (0x2600) palettes; key = runtime Hash(level 0, Hash(u32 fmt,w,h)) then Hash(palette padded with zeros to
  1024 bytes); decode incl. NV2A unswizzle), `rthash.c` (C Hash for ctypes), `index_textures.py` -> 4161 textures.
  **Verified against the live game** (harness TEXKEY=1 log in reference_renderer.cpp): 97/109 palettized matched; the
  unmatched 32-bit ones are per-frame render targets/movie frames.
- `match_pack.py`: 16x16 RGBA thumbnails (colour x alpha), same original size, best vs runner-up.
  Accept: err < 0.004 with runner-up >= 1.6x, or err < 0.01 with runner-up >= 4x (checked by eye on sample sheets).
  Categories used: Stages, Backgrounds, Crates. Result notes/match_env.json.
- `make_pak.py`: 4x the Xbox size (max 1024), full mip chain, BC1 when opaque else BC3, deduped, packed into
  `.pak` files of 10 MB (bigger transfers to the PC fail). **1951 Xbox textures, 1794 images, 177 MB, 19 paks** in
  `Game\textures\replacement\env_NN.pak`.
- Runtime: `sdk/runtime/gpu/texturedumper.cpp` reads `.pak` (ListPak, pak_at, ReadDdsBytes); loose files override.
  Loads stay on the existing background loader (no stutter).
- **HD fonts (made by Claude):** `hd_fonts.py` redraws every English `fonts/<set>/<set>_EN.txd` sheet at 4x from its
  `.met` (METRICS1, codes are Unicode, rects inclusive): cap height/baseline calibrated from the original capitals
  and digits, Noto Sans Mono CJK JP (Medium) drawn per cell, condensed horizontally to the cell, long descenders
  squeezed to fit, white on black. `pak_images.py` packs `<key>.png` images (cap 1). **55 sheets, 1024x1024 BC1,
  `font_00..03.pak` (36 MB).** Only EN made; other languages fall back to the originals.
- **HD menus/UI from the pack:** match_pack on Menu, Pause Menu, HUD, Text, Multiplayer, Stage Names, ... ->
  `notes/match_ui.json` (in xbtex). Diffed every match at full size: EN UI sheets are identical art to the GameCube
  ones (non-EN GameCube sheets differ, rejected; also many false Multiplayer matches on effects, rejected by keeping
  only csdFiles/number/stafroll sources with no differing regions). **81 textures at up to 2048 px (`PAK_MAX_SIDE=2048`), 107 MB, `ui_00..12.pak`.**
- User test (16:04): title logo, its text and backdrop still low res. Cause: TitleParts was rejected by the diff
  check (only compression noise) and 512 sheets were capped at 1024. Fixed: TitleParts added, UI cap 2048, TitleBg
  shipped as pack art plus the Xbox-only black strip (texel-aligned patch). Live keys checked in run2/o_keys.txt:
  title uses TitleAnime a6a16eb7, BG d2d76307, TitleParts 686a1e45.
- **Xbox buttons (made by Claude):** the multi-platform button sheets (`gamemenu`, `pause_main` EN) hold PS, GameCube
  and Xbox buttons side by side. `xbox_buttons.py` takes the pack's HD sheet, clears every button slot and paints a
  glossy Xbox button (Duke colours A green, B red, X blue, Y yellow; dark rim, top highlight, white letter with
  outline) with the slot's meaning (PS x/GC A -> A, PS o -> B, PS triangle/GC Y -> Y, GC X -> X). `buttons_00.pak`.
  Pause/HUD `micon` sheets have no buttons.
- Re-optimization check: all replacement textures are BC1/BC3 with mips (GPU memory ~1/4 to 1/8 of RGBA), loaded by
  the background loader, so no frame-time cost. Paks are read per texture on demand (no full-pak loads).

### Local SDK patches (add to the list above)
- `sdk/runtime/app/game_code.cpp`: `#ifndef __MINGW32__` around `#include "hook_exports.h"`.
- `sdk/runtime/app/settings.cpp`: RenderScale uses the width too when widescreen.
- `sdk/runtime/xdk/d3d8.cpp`: DrawSnapshot / DrawIndexedSnapshot (call-time vertex copy for small draws).
- `sdk/harness/build_rt.sh`: harness build with the render thread on.
- `sdk/runtime/gpu/d3d12.cpp`: BindTargetTexture `kShrinkMin` (wide glow taps only for real shrinks); disk shader +
  pipeline caches, AsyncCompiler, LogHitch.
- `sdk/runtime/gpu/texturedumper.cpp`: .pak texture packs.
- `sdk/harness/reference_renderer.cpp`: TEXKEY=1 logs each texture's replacement key (debug).

### Session 8 (2026-10-31, 19:20 to about 19:45): crash after Westopolis, event skip, events at 60 FPS
- **Crash** (guest write to 00C5D1BC): RenderWare frame code (RwFrame dirty-list link 002E1300 <- 002E1820 <-
  00059A76) touches a frame in pages the game had freed. Fix in `sdk/runtime/kernel/memory.cpp`: Decommit keeps the
  host pages mapped with their bytes (only the commit map forgets them); Commit zeroes pages that were not committed.
  Not reproduced here (needs a cleared Westopolis); user to confirm.
- **Event skip**: the event manager (TPjsEventActionManager, update 00228740) takes START only when byte
  `[0x541F20]+8` is 0; 00226B00 sets it to 1 on the first viewing (save bitmask +0x1754). New hook after 00228794
  (`ShadowEventFirstView`, clears CL). Verified headless: START in event1101 jumps to stg0100.
- **Events at 60 FPS**: the stage loop 00056FC0 busy-waits `[0x54DD40]` periods of 1/[0x54DD64] s (rdtsc / 733 via
  000116B0); event start 002291AA writes 2 (30 FPS), event end 00229353 restores. Hook after 002291AA
  (`ShadowEventFrameRate`) writes 1 when `cutscene_60fps` (new option, GAME tab, default on). Verified headless:
  event1101 at 60 frames/s (was 30). **User checks the cutscene speed**; if double speed, interpolate instead.
- Headless with the user's save: copy `Game\hdd\E\UDATA\53450086` into `<run>/hdd/E/UDATA/`, start
  `/home/claude/hb/shadow /home/claude/rungame` in `<run>` in the background, drive it with `cmd` files
  (`START 4`), screenshots via `/home/claude/tools/snap.sh <run> <png>`; START x6, A, START reaches event1101.
- User test 19:35: crash fixed (stage to stage fine), event skip works. Events show 60 on the counter but still
  look 30; turning the option off did not go back to 30. User asked to stop the 100% core.
- **Option live**: the frame-rate hook moved to the stage loop entry (00056FC0, every frame): while an event plays
  (`[0x54D481]`), `[0x54DD40]` = option ? 1 : 2. Turning it off now gives 30 at once.
- **CPU**: hook in the busy loop (after 0005713E, `ShadowFrameWait`): the wait ends when the clock (rdtsc/733 us)
  crosses `k*period - now%period` from now (k = (target-elapsed)/period; grid of period multiples); sleeps all but
  the last 2.5 ms. Headless: main guest thread 100% -> ~15-20%, still 60 frames a second.
- **Why events still look 30** (found, not fixed): with CAP=1 draw lists (run9), scene world matrices AND the camera
  view change only every 2nd frame at 60. Event time `[0x54D498]` += dt*30 when `[0x54D481]` (0022B876), i.e. +0.5
  a frame at 60, passed as float to the event updaters (camera 0022BDE0 -> 0022BBE0, objects 0022BF80, 0022C410,
  0022CA10, ...). Something samples it at whole 30-FPS frames. Next: find that sampler (keyframe lookup / motion
  player) and interpolate between keys, or run the event at a time scale and interpolate rendering. One
  cvttss2si in event code at 0022BD02 (`[esi+8]`), not yet checked.


### Session 9 (2026-10-31, 19:47 to about 20:00): event smoothness
- Cause 1 (fixed): the event scene player (event+0x180, 0022BDE0 -> 0022BBE0) seeks to the event time cut to a
  whole 30-FPS frame (cvttss2si [esi+8] at 0022BD02, cvtsi2ss at 0022BD2C). Hook after 0022BD2C
  (`ShadowEventSceneTime`) puts the unrounded time in xmm0 when cutscene_60fps. Headless (CAP=1 draw lists): the
  camera view now changes every frame (was every 2nd).
- Measured with CAP=1: CPU-skinned characters (fixed-function, world identity, vertices change) already changed every
  frame. Still every 2nd frame: one rigid object of 3 draws (182/186/1680 verts, vs B00E16C1, own world matrix),
  plus an environment effect. Its matrix is written each update by RwFrame LTM sync 002E2C40 <- 000492C0 <- 00049750
  <- 0022A240 (actor update) <- 0022CA10. Actor placement: 0022A180 finds a node by name (00376420) in
  `[esi+0xA4]+0x20` and copies it (00049930). Next: find what drives that node at whole frames.
- Event time `[0x54D498]` verified advancing 0.5 per 16.7 ms at 60.
- Tools: /home/claude/tools/evrun.sh (run into event1101 from the user's save, env e.g. CAP=1, WAIT=s),
  /tmp/find_alt.py (alternating draws + their matrix in guest memory), gdb `watch *(int*)0x1<guest addr>`.
- Harness intermittent early crash seen again (run14 died at 8.7 s); rerun.

- User test 20:04 (Black Doom hologram event, Westopolis end): still choppy; **event subtitles very faint**
  (whole text incl. outline semi-transparent). Event font = fonts/stg0904 (HD sheet replaced). In events everything
  incl. 2D goes to the 512 scene target 03A2B080 then one blit; the harness CAP lists show no font-sheet draw there
  (probably push-buffer text, BeginPush/EndPush, not listed). Original font sheets: alpha 255 everywhere (like the HD
  ones). Asked the user for an F3 capture (Capture Frame.bat) with a subtitle on screen in a cutscene.

- 20:10: subtitles clear now (the faint one was a moment in that scene). Still choppy. Windows log shows events
  present at a steady 60 (no hitches during event1101/event2101), so it is content, not pacing. Remaining 30 Hz
  movers are actor attachments (0022A240 -> 00049750 LTM writes). Black Doom hologram scene = event2101 (after
  Westopolis). Options menu has a music room; event replay not found yet (menu nav via harness `cmd` + snap.sh).

- **DELIVERY BUG (20:20)**: re-committing the same staged path (/mnt/user-data/outputs/Shadow.exe) can silently
  leave the OLD file on the PC. The 20:15 commit did not land; the PC ran an older exe+dll, so the camera fix / CPU
  fix / live option may never have reached the user. **Rule now: stage each build in a fresh folder
  (/mnt/user-data/outputs/b<HHMMSS>/) and verify md5 by staging the PC file back.** Verified build b202445.
- **New crash** (shadow_016.log, after event2101 at the route screen RouteDisplay): null+0x108 read in 002EC720
  (RwFrame dirty-list walk, list head 0x57DEBC) <- 002DCDE0 <- 00157788. Same use-after-free as the first crash:
  the stage heap is released while a frame in it is still linked; our allocator handed the same addresses straight to
  the next heap, whose data overwrote the link. Fix in memory.cpp: released user regions are quarantined 20 s before
  their addresses are reused (pages keep their bytes); flushed early when the space runs out or a fixed base asks.
- Debug log on: `event timing: time .. elapsed .. wait ..` (first 240 updates of each event), in hooks.cpp
  (DBG-EVENT-TIMING). Remove when done.

- **20:34 user test (verified build b202445): cutscenes at 60 look fine now.** Timing log on Windows: event time
  +0.5 every 16.7 ms, elapsed 1, wait 1 (perfect). After event2101 ends the game sits on a **black screen** (no crash):
  log ends with RouteDisplay_EN.txd / RouteDisplay.xncp opened at 84.27 s, nothing more until the user quit. Same
  spot as the earlier crash (null+0x108 in the RwFrame dirty-list walk), so likely the same stale-frame family, now
  hanging instead of faulting. Asked the user to press F3 (logs every guest thread's stack) on the black screen.
- **User priority from now: performance and fixing bugs like these throughout the game.** Plan: find the
  stale-frame root cause (who frees the stage heap with frames still linked: hook RwFrameDestroy/the heap release
  at stage end, or make the game unlink), then sweep stage transitions (all stages, events, menus) headless.

- 20:38 run (shadow_018.log): no black screen. event2101 -> RouteDisplay (63.45 s) -> stg0201 + event2102 at
  69.7 s, no errors. F3 was pressed during event2101 (21.8 s), so no stacks from a hang. The black-screen run sat
  31 s after RouteDisplay with nothing loading. Intermittent; root cause still the stale-frame family (suspected).
  Idea for next session: an automatic watchdog that logs every guest thread's stack when the main game thread stops
  making progress (no file opens / no stage loop updates for N s while presents continue).
- Load hitches seen: event start 23-39 ms (event texture uploads, frames 938-1146), stage load up to 57 ms (54
  textures). Candidates for the performance pass (upload on the background loader, prewarm).

### Session 10 (2026-10-31, 20:42 to about 21:40): the black screen, found and fixed
- **Reproduced headless**: pausing in Westopolis (pause opens RouteDisplay) hung 2 of 3 runs. Main guest thread
  looping in 002EC720 (RwFrame dirty-list sync, head 0x57DEBC) -> 002EC670 -> 002EC610; the list held a frame
  linked to itself, flags 0 (a frame re-added while still linked).
- **Cause**: guest threads run truly in parallel here; the Xbox had one core. Shadow's loader task threads (thread
  proc 0004F440: virtual step [obj+4] in a loop with a yield, then finish [obj+8]) build RenderWare objects while the
  main thread updates/syncs the same frame lists. Unsafe without the single core.
- **Fix**: a "guest CPU" lock (kernel::GuestCpuAcquire/Release, GuestCpuIdle in objects.cpp/kernel.h). Held by each
  loader task step (hooks after 0004F458 / 0004F45C and 0004F46A / 0004F472) and by the main thread for its frame
  (taken after the frame wait 00057149, let go at the frame wait 00056FC0). Every blocking kernel call (Wait,
  KeDelayExecutionThread, NtYieldExecution) and D3DDevice_Swap let it go while they wait, so a holder that waits
  never stalls the others. Tests: 11 pause/unpause cycles + a pause-menu stage change (stg0100 -> stg0201 +
  event2102), no hang, steady 60 FPS, stage load time unchanged (30.4 s mark).
- Also found: the "intermittent harness crash at frame 300 / 8.7 s" was the same use-after-free with the OLD memory
  behaviour (SHADOW_MEM_ORIG=1 reproduces it); the keep-mapped memory fix had already removed it.
- Quarantine bug fixed: commits to existing reservations (base set, no reserve) flushed the quarantine; now only
  fixed-base reservations do. MEM_RELEASE now follows NT (base must be the reservation base, size 0; refusals logged
  as warnings, none seen).
- **Hang recorder**: if presents stop for 4 s after the first frame, every guest thread's stack is logged once
  ("hang watch:" in the log). F3 still dumps on demand.
- DBG left on: `stale frame:` report in 002E1300 (cheap; never fired). Event timing log removed.
- Delivered build b210056, then exe b210428 (hitch lines now add texture create/upload/pack ms) (md5 verified).
- Sweep (headless, user's save): stg0100 -> pause-menu stage change -> stg0201 + event2102 (skipped) -> stg0201
  play with 3 pause cycles: 60 FPS throughout, no hang, no stale frame, no exception.
- Next performance item: first-use texture uploads at event/stage start (9-54 textures in one frame, 23-57 ms,
  inside fades/loads). Options: create D3D12 textures from a placed-resource heap, or prepare them on the
  background loader before first use.

- 21:14 user test: no crashes. Requests: HD weapon prompt (gun + X), quit to desktop, pack fonts instead of mine.
- **HUD textures were never indexed**: they live inside csdFiles/*.xncp (RW TXDs embedded in the layouts):
  001_command_icons_xbox (weapon icons + X/A/B), 000_windowitem (ring/face/key icons, key 8a06c7d9...),
  000_squarebuttons, 000_VHArrow_xbox, 000_hud_meter, mchara_* portraits, loading, results, logos, ...
  index_textures.py now reads .xncp (62 new textures). Matched to the pack (notes/match_xncp_keep.json); non-EN and
  wrong-language matches dropped. The three button sheets get the pack's HD art with Xbox buttons painted in
  (xbox_buttons.py: all slots cleared first, incl. the pack's own off-position buttons, then painted).
  Shipped hud_00..02.pak (23 textures) + buttons_00.pak (7 sheets).
- **Pack-style fonts**: tools/host/pack_fonts.py harvests each character's 4x glyph from the pack's GameCube font
  sheets (cell-by-cell match against the Xbox glyphs, 86 characters found; blanks/full-width ？ keep mine) and
  rebuilds the 55 Xbox sheets. Shipped as font_00..03.pak (my earlier HD fonts stay in xbtex/paks_font to switch back).
- **QUIT TO DESKTOP** row in the GAME tab (press twice; posts WM_CLOSE).
- Harness note: one run died silently at 11 s right after the first stage-heap release (REF+TEXKEY on); not seen
  again in 6 later runs. Keep an eye out.
- Delivery d212431 (exe + 8 paks), md5 verified.

- 21:36 user: "ran perfect, a few stutters". shadow_021.log hitches:
  1. **Stage load froze the picture 2.2 s + 3.8 s + 0.5 s** (stg0100 -> event2101, was ~0.6 s before the guest CPU
     lock). Cause: the main thread took the lock at the end of each frame wait and kept it when it left the stage loop
     to load, so the loader tasks only ran in the gaps. Fix: the main thread holds it only inside the stage frame step
     (hooks before 0001D805 `call 00057630` / after 0001D80A, plus the wait release/retake inside). Headless:
     stg0100_cmn -> event2101 0.1 s; 9 pause cycles, no hang.
  2. **~2 s of uneven frames in gameplay** (264 s: every other frame 30 ms, the next ~3 ms): two clocks. The game's own
     60 Hz rdtsc grid drifted against the port's pacing/V-Sync. Fix (Windows only): with fps cap 60 and the game
     waiting one period, ShadowFrameWait ends the wait at once as if one period passed (dt exactly 1/60), so the
     game does one update per present; event frames at 30 (two periods) keep the game's wait. And app::Pace skips its
     own clock when V-Sync is on at a refresh within 1 Hz of the cap.
  3. Event start uploads (up to 54 textures, 46 ms at event2102: create 18 ms, pack 11 ms, upload 3 ms). Next:
     placed-resource heap for textures and pack lookups off the render thread.
- Delivered b214457 (md5 verified).

- **2026-11-01 11:33 user: REGRESSION**: with V-Sync ON the game ran far above 60 and sped up; fine with V-Sync off.
  Cause: the render thread decouples presents from V-Sync, so app::Pace skipping itself under V-Sync left nothing
  holding the guest to 60 once ShadowFrameWait ended the game's own wait. **Both pacing changes reverted** (Pace
  always paces; the game's wait is kept, only slept through). The load-freeze fix (lock scope) stays.
- User also saw faint subtitles + quieter audio at places (event3101, stg0301) and pressed F3 (plain launch, so only
  stacks, at 142-172 s; nothing conclusive). **F3 now always saves a frame capture** (default dir Game\captures)
  as well as the stacks, so the next F3 on faint subtitles gives the subtitle draws' vertex colours and blend.
- Delivered exe b113600 (md5 verified; dll unchanged from b214457).

## Plan for the next sessions (set by the user)
Done: cutscene skip (session 4), stage resolution (session 4, to confirm).
Remaining, in order:
0. **Confirm session 7 on Windows**: HD stages, HD fonts (subtitles), HD menus, Xbox buttons in menus/pause.
   If a texture looks wrong, `Game\logs` + a screenshot; a bad key can be dropped by rebuilding its pak.
1. **Confirm on Windows**: stages sharp now? cutscene skip works for movies AND in-engine events? Read `Game\logs`.
2. Blur fix (session 5) to confirm; then texture pack (user downloads Cyb3rtron's "Shadow the Hedgehog HD" pack
   zip into ShadowNativePort; match GameCube images to dumped Xbox textures by image similarity, write to the
   runtime's textures\replacement\ folder) and HD fonts.
3. **Cutscene rendering issues** (in-engine events).
4. **HD HUD / GUI / subtitles** (2D at native resolution, widescreen placement).
5. Later: 60 FPS+ (frame rate options stop at 60 until game timing is handled), ultrawide.

- Exe icon (16:25): the game's red emblem from the title sheet, `tools/winbuild/res/shadow.{ico,rc}`, linked by build_win.sh.

### User test 16:25
- Ring icon fine now; **in-engine cutscene skip confirmed working.**
- User decisions: English-only HD fonts are enough (no other languages). Ultrawide and above-60 FPS deferred until
  after the user's first full playtest. **Next: user playtests and reports issues** (screenshots / F3 captures via
  `Game\Capture Frame.bat`).

### Playtest 16:36 (for session 8)
User's order for session 8 (wait for the user to say start): fix the crash, fix the event skip, then unlock the
event frame rate to 60 first (just the unlock); the user checks for double speed; only if so, add interpolated
in-between frames.
1. In-engine cutscenes do NOT skip (16:25 "do skip" was a misread). Hook at 0022879B never seen running; trace the
   event START path on Windows.
2. In-engine cutscenes run at 30 FPS; user wants 60. Find the event frame-interval switch; check animation speed.
3. **Crash after Westopolis + its events** (log shadow_011.log): C0000005 on guest thread 14 (main), write to guest
   00C5D1BC (unmapped), eip host 7FF8C95EF74F; stack 002E1866 00059A7B 0039BA18 00059EC3 0039BA3B 00021E2A 00057247
   0005725A 00021E32 00057730 0001D80A 00011294 00011C60. Just before: stg0100_*.one loads, `open d:\sng_EV2101_E.adx`
   then `open d:\` (empty name). Plan: map 002E1866's caller chain, find who owns 00C5D1BC (freed/decommitted heap?),
   reproduce headless from the user's save (hdd\E\UDATA) to start right at that point.

### Still open
- **HUD ring icon missing on Windows** (user, 16:10). It is a 2D sprite: back buffer draw, tex 128x64 P8 with a
  32-colour palette (palette Common C00B0001), runtime key 8a06c7d9402cb96b, at (57,364), pretransformed with rhw 1
  (the other HUD sprites have rhw inf), alpha test + blend 302/303. Drawn fine by both harness builds (plain and render
  thread), not replaced by any pak (checked), texture and palette stable all run (one key). So: D3D12 backend only.
  Next: user runs `Game\Capture Frame.bat` (sets XBE_CAPTURE_DIR=captures), presses F3 in a stage; read
  `Game\captures\<time>\frame.txt` + images for that draw.
  **Capture 16:20 (no subtitle on screen): the ring IS drawn on Windows** (draws 0210-0212, t0 834BCF80; back buffer
  and final frame show it). The user's screenshot had a subtitle up, so it may vanish only during dialogue (maybe the
  original game's behaviour). Asked the user.
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
- **Fully native**: yes, by design.
- **Best-quality PC port:** resolution options (1440p, 4K and beyond, native internal resolution, not upscaled),
  graphics settings menu (resolution, window mode, V-Sync, AA, texture filtering), proper widescreen (ultrawide as a stretch goal),
  solid **60 FPS**, and **above 60 FPS** later (needs game-logic timing decoupled or interpolated; do after it is playable).
- **Efficiency:** keep sessions lean; test only what changed. **The user asked Claude to do all building** (cross-build here, ship exe+dll).
- Order: boot, then playable, then PC polish.

## Session 11 (2026-11-01): texture false matches, Cosmic Fall smear, faint subtitles
- Glyphic Canyon "lightning" streaks were a texture-pack false match: the Black Comet lightning sheet had been accepted for 4 keys, including the rain streaks s0201_sa002.
  - New tools/host/verify_matches.py compares at full Xbox size (alpha-weighted MSE / mean variance).
  - All 23 matches with rel > 0.88 were false (checked on contact sheets); the 0.25–0.88 band is real HD remasters.
  - Dropped those 23, plus ef_s0300_011 (orange glow to white) → match_env_v2.json, paks_env2 (19 paks, 1927 textures).
- Cosmic Fall bottom-right white/rainbow smear: the glow target (512x512) is filled each frame by a screen-sized pre-transformed copy quad. Its last rows never get overwritten, so the blur added back each frame piles up there.
  - Fix in src/common/render_ext.cpp Bound(): before the first draw of a frame into a 512x512 colour target, if that draw is pre-transformed, has no blending and no depth buffer, clear the target.
  - Not confirmed in game yet: the user should check Cosmic Fall.
- Delivered: exe md5 d3e1e4fc, env paks (env_00 md5 be9edb2d).
- Faint/ghosted subtitles (captures 124809-124814, ARK cutscene): the subtitle font (P8 256x256 at 8398D780) had been loaded where an old 256x256 render target lived, so it was bound as that target, using stale or wrongly uploaded pixels. In d3d12.cpp BindTexture, 8-bit (P8) textures are now never bound as targets. Delivered as exe md5 25fc8a5f.
- Checked game-wide. All three fixes are in shared code, so they apply to every level and cutscene:
  - The env paks are filtered for every stage.
  - The glow clear applies to any 512x512 target.
  - The P8 rule covers every texture.
  - The UI paks were already restricted to csdFiles sources; verify_matches confirms no false UI matches in use. Non-EN sheets mapped to EN art are unused. The HUD matches are all fine (max rel 0.37).
- Cost: one compare per texture bind; one hash lookup per draw into 512x512 targets only; 24 fewer pack textures to load.

## Session 12 (2026-11-01): release, installer, texture pack drop-in
- **Release carries no game code:**
  - `RELEASE=1 tools/winbuild/build_win.sh exe` builds without XBE_PREBUILT_GAME_CODE and delay-loads Shadow.dll (`-Wl,--delayload=Shadow.dll -ldelayimp`), so the exe starts without the DLL.
  - On the first start, game_code.cpp builds the DLL from the player's disc using the compiler folder:
    - llvm-mingw 20251104 Windows `clang-21.exe` → clang.exe and `ld.lld.exe` → lld-link.exe, with libLLVM-21, libclang-cpp, libc++ and libunwind.
    - xbe_recomp.exe cross-built with mingw, with Zydis and toml++ static.
    - config, include/cpu, include/freestanding, xdk layouts.
    - Shadow.lib = host.lib from host_def.py.
  - Pipeline simulated on Linux with the same flags: translate, 129 compiles and link give a DLL of the same size as the shipped one.
- **Texture pack drop-in:** src/common/pack_import.cpp, called from InitRender before anything loads.
  - Takes a zip (or an extracted folder) in textures\pack. A zip is unpacked with Windows tar.exe into _unpacked; under Wine, install.sh unzips it instead.
  - textures\pack_map.txt lists 2031 keys: env 1927, ui 81, hud 23. It is generated from match_env_v2, the ui paks and hud_match.
  - Each image: decode DXT, box scale (4x Xbox size, 1024 env / 2048 UI), full mips, own BC1/BC3 encoder. Output is pack_import.pak with a stamp file.
  - Tested on Linux: the PRISON ISLAND title card comes out the same as the source.
  - Derived art is left out: Xbox button painting, pack-style fonts, TitleBg patch. The release ships Claude's Noto HD fonts (OFL).
- **Setup.exe** (tools/installer/setup.cpp, Win32):
  - Inputs: install folder (Program Files refused), disc (.iso or a default.xbe folder), optional pack zip.
  - Copies files/ there, the disc into assets, and the pack into textures\pack.
  - Makes desktop and Start menu shortcuts.
- **Linux zip:** install.sh / play.sh / README-Linux.txt run through Proton or Wine 9+ (vkd3d). A native Linux build would need Vulkan and audio backends, which were not done.
- **Release packaging:** tools/release/make_release.sh <ver> → ShadowNativePort-v1.0-Windows.zip and -Linux.zip, about 54 MB each. Neither contains game code or data.
- Not tested on a real PC yet: the first-start compile on Windows, Setup.exe, and the Wine path.
- After the user's test install (14:05):
  - **Save messages:** the English save messages are reworded for PC. kernel PatchGameFile rewrites fonts\Save\Save_EN.fnt and fonts\Advertise\Advertise_EN.fnt as the game opens them (src/common/pc_text.cpp), writing to cache\patched.
    - Rules match the original wording.
    - Only characters the font already has are allowed.
    - 25 entries changed per file, and the "no save" prompt now reads "Would you like to create save data?"; checked in the harness.
    - Other languages are unchanged.
  - **Xbox buttons:** the texture pack import now paints them too, from tools/release/pack_buttons.bin (tools/host/make_button_data.py).
    - It stores only the pixels the painting changed: 6 sheets, 720 KB.
    - TitleBg is left out, because its strip came from the Xbox texture.
    - The import stamp has "v2 " in front, so existing installs import again.

## Session 13 (2026-11-05): v1.2 release
- #1 confirmed fixed by duartepedro03 with the v1.2 test build (Goal Ring squares). He asked about Dolphin-style cheats
  (max rings, fill Dark/Hero gauge); owner replied "working on new features".
- **#3 keyboard rebinding** ported from Sonic Heroes PC: `src/common/keyboard.{h,cpp}` with an action table and
  settings.toml `[keyboard_bindings]` (game_info load/save hooks; settings saved at start so the table is there to
  edit). Options menu CONTROLS tab lists the bindings in use (two columns). CONTROLS.txt: how to change keys + names.
  Defaults unchanged. Not tested on Windows yet (rebinding needs the Windows toml build).
- Release v1.2 (tools/release/make_release.sh 1.2), notes in /home/claude/release/RELEASE_NOTES_v1.2.md.
- Released v1.2 (user published it); #1 closed, #3 answered (left open). **Next for Shadow (after Heroes):** rebinding
  keys from inside the F1 menu (promised on #3), plus more QOL; cheats like duartepedro03 asked (max rings, fill
  Dark/Hero gauge) are a candidate.

## Session 14 (2026-11-08): true widescreen (#4), test build v1.3
- #4 (XenonCF): 4:3 picture stretched on 16:9. duartepedro03 also asks for the HUD at the screen edges.
- **Hor+ 3D:** RwCameraSetViewWindow is at **0x2DD1C0** in Shadow (same bytes as Heroes' 0x2A4C20; window at camera
  +0x68, reciprocal +0x70). Hook in src/game/hooks.cpp widens a 4:3 window by (16/9)/(4/3) once per camera (map
  of the window it gave each camera, so the game re-setting it is not widened twice). Setting `widescreen` (default
  on), menu GRAPHICS > WIDESCREEN (takes effect when the game next sets its cameras).
- **2D:** SDK gpu/vertex.cpp `HudSqueeze` (+ shaders.cpp fixed-function XYZRHW path, d3d12 zparams.w, harness
  reference renderer): screen-space draws onto the 640-wide screen that do not span its whole width are narrowed by
  0.75 toward the centre. Full-width draws stay (the 3D scene is a full-screen quad from the 512x512 scene target onto
  the back buffer; fades, movies). Targets other than 640 wide (scene 512, glow 256/128/64) untouched.
  Edge anchoring (left/centre/right per draw) is coded behind `gpu::g_hud_anchor_edges` (off): text is drawn one
  glyph per draw, so per-draw anchors would split centred lines. Needs string grouping, then the HUD can go to the
  edges (duartepedro03's request).
- **Untested:** the game data is no longer here (rungame gone, no ISO on the PC). Test build
  /home/claude/release/Shadow-v1.3-test.zip (Shadow.exe, version 1.3) for the user / issue reporters.
- Unknowns to check in the test: does the stage HUD draw onto the 640 screen (then it is narrowed) or into the 512
  scene target like event 2D (then it stays stretched); any 4:3 render-to-texture camera widened by mistake.
- User captures (10:07-10:10, v1.3 test): 3D view correct, HUD/menus/text keep shape centred; full-screen backgrounds
  and scene stay full width. HUD bunched in the middle (ring counter overlaps the subtitle text).
- **Edge anchoring v1** (gpu/vertex.cpp HudLayout): screen 2D draws grouped per frame into blocks (same texture,
  continuing a line or the next line); at present each block gets an anchor: an earlier block containing it (panel)
  passes its own, else left half -> left edge, right half -> right edge, crossing the middle -> centre. Used the next
  frame by key (texture, first piece position). Only when the previous frame drew the 3D scene (full-width quad from a
  512x512 texture); menus stay centred. F3 capture lines now show `hud <code>` and screen-space positions.
  Installed straight into the user's Shadow folder for testing.
- 12:31 captures were from the OLD exe: device_commit_files with a reused stagedPath wrote the earlier file (size
  6898688 vs 6903808). **Always commit via SendUserFile fileUuid or a fresh staged path, then check the size.**
- Found from those captures: all Shadow 2D (HUD, menus, text) is drawn through vertex programs B0000061/B0000071
  (oPos.xyz = v0.xyz * c[96] + c[97], oPos.w = rcp(v0.w)), not XYZRHW, so the squeeze never touched it.
  d3d12 `ScreenProgram()` recognises such programs from their translation (once per program, logged
  "d3d12: vertex program ... is screen space 2D"); HudSqueeze takes x = v0.x * c96.x + c97.x; the squeeze now runs in
  kVsEpilogue on p.x for both fixed and program paths. Captures: positions printed for every buffer draw, `hud` code.
  Harness reference renderer does not apply it to programs (no game data here to test anyway).
