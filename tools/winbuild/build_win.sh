#!/usr/bin/env bash
# Cross-builds the Windows port on Linux, no Visual Studio needed.
#   Shadow.exe : SDK runtime (window, D3D12, XAudio2, SDL3 pads, ImGui) - llvm-mingw (GNU ABI)
#   Shadow.dll : the recompiled game code - clang/lld-link targeting the MSVC ABI, freestanding (no Windows SDK)
# The two meet only through extern "C" functions. The DLL's C++ EH and CRT calls go to vcruntime140/ucrtbase
# through the exe's forwarding exports, so the player needs the VC++ 2015-2022 redistributable (most PCs have it).
#
# usage: tools/winbuild/build_win.sh [stage...]   stages: exe dll pack (default: all)
# env: MINGW (llvm-mingw root), DEPS (imgui, SDL install, toml++), OUT (output dir), JOBS
set -euo pipefail
cd "$(dirname "$0")/../.."
REPO=$(pwd)
MINGW=${MINGW:-$(ls -d /home/claude/xtool/llvm-mingw-*/ | head -1)}
DEPS=${DEPS:-/home/claude/winbuild}
RELEASE=${RELEASE:-0}  # 1: no game code shipped, the exe builds Shadow.dll on the player's PC (delay loaded)
if [ "$RELEASE" = 1 ]; then OUT=${OUT:-/home/claude/winbuild/rel}; else OUT=${OUT:-/home/claude/winbuild/out}; fi
JOBS=${JOBS:-$(nproc)}
GEN=$REPO/generated/game
RT=$REPO/sdk/runtime
IMGUI=$DEPS/deps/imgui
SDL=$DEPS/sdl-install
TOML=${TOML:-/home/claude/toolbuild/build/_deps/tomlplusplus-src/include}
CXX="${MINGW}bin/x86_64-w64-mingw32-clang++"
STAGES=${*:-exe dll pack}
mkdir -p "$OUT/obj/exe" "$OUT/obj/dll" "$OUT/bin"

has() { [[ " $STAGES " == *" $1 "* ]]; }

# ---------------------------------------------------------------- exe
if has exe; then
    XBE_SHA1=$(sha1sum "$REPO/assets/default.xbe" | cut -d' ' -f1)
    echo "extern const char kImageSha1[] = \"$XBE_SHA1\";" > "$OUT/obj/exe/xbe_hash.cpp"
    SRC=$(ls "$RT"/*/*.cpp "$REPO"/src/common/*.cpp "$REPO"/src/game/*.cpp)
    SRC="$SRC $IMGUI/imgui.cpp $IMGUI/imgui_draw.cpp $IMGUI/imgui_tables.cpp $IMGUI/imgui_widgets.cpp"
    SRC="$SRC $IMGUI/backends/imgui_impl_win32.cpp $IMGUI/backends/imgui_impl_dx12.cpp $OUT/obj/exe/xbe_hash.cpp"
    FLAGS="-std=c++20 -O2 -g0 -march=x86-64-v2 -fno-strict-aliasing -fms-extensions -Wno-everything
        -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS $( [ "$RELEASE" = 1 ] || echo -DXBE_PREBUILT_GAME_CODE ) -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -DNTDDI_VERSION=0x0A000008
        -I$REPO/src -I$RT -I$GEN -isystem $IMGUI -isystem $TOML -isystem $SDL/include"
    export CXX FLAGS
    # any header of the runtime, the game or the generated image (image_symbols.h carries the hand-mapped
    # addresses) rebuilds every object
    NEWEST_H=$(find "$RT" "$REPO/src" "$GEN/image_symbols.h" "$GEN/hook_exports.h" -name '*.h' -printf '%T@ %p\n' \
        | sort -n | tail -1 | cut -d' ' -f2)
    fail=0
    objs=()
    for f in $SRC; do
        o="$OUT/obj/exe/$(echo "${f#/}" | tr / _ | sed 's/\.cpp$/.o/')"
        objs+=("$o")
        if [ ! "$o" -nt "$f" ] || [ ! "$o" -nt "$NEWEST_H" ]; then
            ( $CXX $FLAGS -c "$f" -o "$o.tmp" 2> "$o.err" && mv "$o.tmp" "$o" || echo "FAILED $f" ) &
            [ "$(jobs -r | wc -l)" -ge "$JOBS" ] && wait -n
        fi
    done
    wait
    for o in "${objs[@]}"; do [ -f "$o" ] || { echo "== $o"; head -20 "$o.err"; fail=1; }; done
    [ $fail = 0 ] || exit 1

    # import library for the exe's calls into the game DLL (GNU format, the exe is mingw)
    "${MINGW}bin/llvm-dlltool" -m i386:x86-64 -d "$GEN/imports.def" -l "$OUT/obj/libshadow_code.a"
    # the icon (resource 1, which the runtime's windows load): the game's own emblem from the title sheet
    "${MINGW}bin/x86_64-w64-mingw32-windres" -I "$REPO/tools/winbuild/res" "$REPO/tools/winbuild/res/shadow.rc" -O coff \
        -o "$OUT/obj/exe/shadow_res.o"
    objs+=("$OUT/obj/exe/shadow_res.o")
    SDL_LIBS="$SDL/lib/libSDL3.a -lsetupapi -limm32 -lversion -lcfgmgr32 -lwinmm -lole32 -loleaut32 -luuid -lshell32 -ladvapi32 -lgdi32 -luser32"
    $CXX -o "$OUT/bin/Shadow.exe" "${objs[@]}" "$OUT/obj/libshadow_code.a" -mwindows -municode -static -fuse-ld=lld \
        -Wl,--out-implib,"$OUT/obj/libShadow_exe.a" $( [ "$RELEASE" = 1 ] && echo -Wl,--delayload=Shadow.dll -ldelayimp ) \
        -ld3d12 -ldxgi -ldxguid -ld3dcompiler -lxaudio2_9 -lole32 -lwindowscodecs -ldbghelp -lwinmm -ldwmapi -lws2_32 \
        $SDL_LIBS
    echo "built $OUT/bin/Shadow.exe"
fi

# ---------------------------------------------------------------- dll
if has dll; then
    DFLAGS="--target=x86_64-pc-windows-msvc -std=c++20 -fms-compatibility-version=19.40 -O2 -march=x86-64-v2
        -fno-strict-aliasing -nostdinc -nostdinc++ -w -DX86_GAME_CODE -I$RT -I$GEN -isystem $RT/cpu/freestanding"
    export DFLAGS GEN
    ls "$GEN"/x86_recomp.*.cpp "$GEN"/x86_func_mapping.cpp | xargs -P "$JOBS" -I{} sh -c '
        o="'"$OUT"'/obj/dll/$(basename {} .cpp).obj"; [ "$o" -nt {} ] && exit 0
        clang++ $DFLAGS -c {} -o "$o.tmp" && mv "$o.tmp" "$o"'
    # MSVC-format import library of what the exe exports, with the DLL's C++ imports mapped to the exe's
    # Itanium-mangled names (tools/winbuild/host_def.py)
    "${MINGW}bin/llvm-readobj" --coff-exports "$OUT/bin/Shadow.exe" | awk '/Name: /{print $2}' > "$OUT/obj/exe_exports.txt"
    llvm-nm --undefined-only "$OUT"/obj/dll/*.obj 2>/dev/null | awk '{print $2}' | grep '^__imp_' | sed 's/^__imp_//' \
        | sort -u > "$OUT/obj/dll_imports.txt"
    python3 -I "$REPO/tools/winbuild/host_def.py" "$OUT/obj/exe_exports.txt" "$OUT/obj/dll_imports.txt" "$OUT/obj/host.def"
    lld-link /lib /machine:x64 /def:"$OUT/obj/host.def" /out:"$OUT/obj/host.lib" > /dev/null
    lld-link /dll /noentry /nodefaultlib /opt:noicf /machine:x64 /out:"$OUT/bin/Shadow.dll" \
        /def:"$GEN/exports.def" "$OUT"/obj/dll/*.obj "$OUT/obj/host.lib"
    echo "built $OUT/bin/Shadow.dll"
fi

# ---------------------------------------------------------------- pack
if has pack; then
    cp "$REPO/PROGRESS.md" "$OUT/bin/" 2>/dev/null || true
    ls -la "$OUT/bin"
fi
