# Windows cross-build (Linux host, no Visual Studio)

- Toolchain: llvm-mingw 20251104 (ucrt, ubuntu-22.04 x86_64), from GitHub releases:
  `https://github.com/mstorsjo/llvm-mingw/releases/download/20251104/llvm-mingw-20251104-ucrt-ubuntu-22.04-x86_64.tar.xz`
  -> extract to /home/claude/xtool/
- Deps in /home/claude/winbuild/deps: `git clone --depth 1 -b v1.91.0 https://github.com/ocornut/imgui.git imgui`,
  `git clone --depth 1 -b release-3.4.16 https://github.com/libsdl-org/SDL.git SDL`
- SDL3 static: `cmake -S deps/SDL -B sdl-build -G Ninja -DCMAKE_TOOLCHAIN_FILE=<this dir>/mingw.cmake -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_RENDER=OFF -DSDL_GPU=OFF -DSDL_CAMERA=OFF -DSDL_EXAMPLES=OFF -DSDL_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/home/claude/winbuild/sdl-install && ninja -C sdl-build install`
  (mingw.cmake hard-codes the llvm-mingw path; adjust if the version differs)
- Build: `JOBS=2 tools/winbuild/build_win.sh exe dll` -> /home/claude/winbuild/out/bin/Shadow.exe + Shadow.dll
  - needs `assets/default.xbe` (for its SHA-1) and generated/game from xbe_recomp
  - DLL stage: after compiling objects, collect the DLL's `__imp_` imports
    (`llvm-nm --undefined-only out/obj/dll/*.obj | grep __imp_`) into out/obj/dll_imports.txt BEFORE host_def.py
    (build_win.sh's dll stage still writes host.def without the C++ name mapping: run host_def.py by hand, see PROGRESS.md)
- Why two toolchains: exe = mingw (has D3D12/XAudio2/DXGI headers+libs), DLL = clang MSVC target, freestanding
  (needs no SDK). They link through extern "C" + 6 C++ runtime entry points mapped by `host_def.py` (def `==` import names).
- Runtime needs on the player's PC: Windows 10/11, D3D12 GPU, VC++ 2015-2022 redistributable (vcruntime140.dll,
  used by the game DLL's C++ exception handling through the exe's forwarders).
