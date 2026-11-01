SHADOW THE HEDGEHOG - NATIVE PC PORT on LINUX / STEAM DECK
===========================================================

The port is a Windows program (Direct3D 12). On Linux it runs through Wine or Proton, which translate
Direct3D 12 to Vulkan (vkd3d-proton). No game files come with this download: you need your own disc image.

INSTALL
  ./install.sh "/path/to/Shadow the Hedgehog.iso" [/path/to/shadow_the_hedgehog_hd.zip] [install folder]
  (default install folder: ~/Games/Shadow the Hedgehog; the texture pack is optional)

PLAY, option 1: Steam / Proton (recommended, also Steam Deck)
  Steam > Games > Add a Non-Steam Game > Browse > choose Shadow.exe in the install folder.
  Then: Properties > Compatibility > Force a specific Steam Play tool > Proton 9 (or newer, or Proton Experimental).

PLAY, option 2: Wine
  Needs Wine 9 or newer and a Vulkan driver. Run play.sh in the install folder.
  If the picture stays black, install vkd3d-proton into the Wine prefix (winetricks vkd3d).

FIRST START
  The game builds its code from your disc (compiler folder, through Wine). It takes several minutes, once.

The folder can be moved or renamed. Saves are in hdd/.
