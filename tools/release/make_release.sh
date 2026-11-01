#!/bin/bash
# Builds the v<version> release zips (no game code, no game data) into /home/claude/release.
# Needs: RELEASE=1 build of Shadow.exe (tools/winbuild/build_win.sh), the normal build's obj/dll_imports.txt
# (for Shadow.lib), llvm-mingw Windows-hosted toolchain extracted at $LLVMWIN (clang, lld), and the HD font paks.
set -euo pipefail
VER=${1:-1.0}
REPO=$(cd "$(dirname "$0")/../.." && pwd)
M=$(ls -d /home/claude/xtool/llvm-mingw-*/)bin
LLVMWIN=${LLVMWIN:-/home/claude/dl/llvmwin/llvm-mingw-20251104-ucrt-x86_64/bin}
REL=/home/claude/winbuild/rel; OUT=/home/claude/release; F=$OUT/files
RELEASE=1 bash "$REPO/tools/winbuild/build_win.sh" exe
# compiler folder: what game_code.cpp runs on the player's PC
C=$F/compiler; rm -rf "$F"; mkdir -p $C/config $C/include/cpu $C/include/freestanding $F/assets $F/textures/pack $F/textures/replacement
cp $LLVMWIN/clang-21.exe $C/clang.exe; cp $LLVMWIN/ld.lld.exe $C/lld-link.exe
cp $LLVMWIN/libLLVM-21.dll $LLVMWIN/libclang-cpp.dll $LLVMWIN/libc++.dll $LLVMWIN/libunwind.dll $C/
cp /home/claude/winbuild/recomp/xbe_recomp.exe "$REPO/sdk/tools/LLVM-LICENSE.txt" $C/
cp "$REPO/config/game.toml" $C/config/
cp "$REPO/sdk/runtime/cpu/x86_ops.h" "$REPO/sdk/runtime/cpu/x86_context.h" $C/include/cpu/
cp "$REPO"/sdk/runtime/cpu/freestanding/* $C/include/freestanding/
for d in "$REPO"/sdk/xdk/*/; do mkdir -p $C/xdk/$(basename $d); cp $d/layout.toml $C/xdk/$(basename $d)/; done
$M/llvm-readobj --coff-exports $REL/bin/Shadow.exe | awk '/Name: /{print $2}' > $REL/obj/exe_exports.txt
python3 -I "$REPO/tools/winbuild/host_def.py" $REL/obj/exe_exports.txt /home/claude/winbuild/out/obj/dll_imports.txt $REL/obj/host.def
lld-link /lib /machine:x64 /def:$REL/obj/host.def /out:$C/Shadow.lib > /dev/null
cp $REL/bin/Shadow.exe "$REPO/CONTROLS.txt" "$REPO/tools/release/text/README.txt" $F/
cp "$REPO/tools/release/text/PUT_YOUR_GAME_HERE.txt" $F/assets/
cp "$REPO/tools/release/text/PUT_TEXTURE_PACK_HERE.txt" $F/textures/pack/
cp "$REPO/tools/release/pack_map.txt" $F/textures/
cp /home/claude/xbtex/paks_font/font_0*.pak $F/textures/replacement/
cp "$REPO/sdk/THIRD_PARTY.txt" $F/THIRD_PARTY.txt
printf '\nHD font sheets: drawn from Noto Sans Mono CJK JP, SIL Open Font License 1.1.\nCompiler folder: LLVM (llvm-mingw), Apache 2.0 with LLVM Exceptions, see compiler/LLVM-LICENSE.txt.\n' >> $F/THIRD_PARTY.txt
# Setup.exe
S=/home/claude/winbuild/setup; mkdir -p $S
(cd "$REPO/tools/installer" && $M/x86_64-w64-mingw32-windres setup.rc -O coff -o $S/setup_res.o)
$M/x86_64-w64-mingw32-clang++ -std=c++20 -O2 -municode -mwindows -static "$REPO/tools/installer/setup.cpp" $S/setup_res.o \
    -o $S/Setup.exe -lole32 -luuid -lshell32 -lcomctl32 -lcomdlg32
rm -rf $OUT/win $OUT/linux; mkdir -p $OUT/win $OUT/linux
cp $S/Setup.exe $F/README.txt $OUT/win/; ln -s ../files $OUT/win/files
cp "$REPO"/tools/release/linux/* $OUT/linux/; chmod +x $OUT/linux/*.sh; ln -s ../files $OUT/linux/files
rm -f $OUT/ShadowNativePort-v$VER-*.zip
(cd $OUT/win && zip -q -r -9 ../ShadowNativePort-v$VER-Windows.zip Setup.exe README.txt files)
(cd $OUT/linux && zip -q -r -9 ../ShadowNativePort-v$VER-Linux.zip install.sh play.sh README-Linux.txt files)
ls -la $OUT/*.zip
