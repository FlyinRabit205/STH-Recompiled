#!/bin/sh
# starts the game through Wine (needs Wine 9 or newer with vkd3d for Direct3D 12, a Vulkan GPU driver)
cd "$(dirname "$0")"
if ! command -v wine >/dev/null 2>&1; then
  echo "Wine is not installed. Install wine (9.0 or newer), or add Shadow.exe to Steam as a non-Steam game and"
  echo "run it with Proton (see README-Linux.txt)."; exit 1
fi
export WINEPREFIX=${WINEPREFIX:-"$PWD/.wine"}
export WINEDEBUG=-all
exec wine Shadow.exe "$@"
