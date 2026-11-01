#!/bin/sh
# Shadow the Hedgehog native PC port - Linux install (runs the Windows build through Wine or Proton)
# usage: ./install.sh <your game .iso or extracted folder> [HD texture pack .zip] [install folder]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
DISC=$1; PACK=$2; DEST=${3:-"$HOME/Games/Shadow the Hedgehog"}
if [ -z "$DISC" ] || [ ! -e "$DISC" ]; then
  echo "usage: $0 <your game .iso or extracted folder> [HD texture pack .zip] [install folder]"; exit 1
fi
mkdir -p "$DEST"
cp -r "$HERE/files/." "$DEST/"
if [ -d "$DISC" ]; then cp -r "$DISC/." "$DEST/assets/"; else cp "$DISC" "$DEST/assets/"; fi
if [ -n "$PACK" ]; then
  # unpacked here: Wine has no tar.exe for the game to unzip it itself
  mkdir -p "$DEST/textures/pack/hd" && unzip -q -o "$PACK" -d "$DEST/textures/pack/hd"
fi
cp "$HERE/play.sh" "$DEST/"; chmod +x "$DEST/play.sh"
echo "Installed to $DEST"
echo "Start it with: \"$DEST/play.sh\"  (the first start builds the game code, a few minutes)"
