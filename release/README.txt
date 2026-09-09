!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
WARNING: MEMORY CARD SUPPORT IS EXPERIMENTAL
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

DO NOT USE A MEMORY CARD CONTAINING SAVES OR OTHER DATA YOU CARE ABOUT.

MEMORY CARD SAVE/CONFIG WRITING HAS NOT YET BEEN VALIDATED ON REAL
HARDWARE. UNTIL IT HAS BEEN PROVEN SAFE, USE A DISPOSABLE/TEST CARD
ONLY.

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

DoomCube
========

DOOM on the Nintendo GameCube.


REQUIREMENTS
------------

Python 3 is required to build a DoomCube ISO.

Windows:
  Install Python 3 from https://www.python.org/ if it is not already
  installed.

Linux:
  Python 3 is already installed on many distributions. If it is not
  available, install it using your distribution's package manager.

macOS:
  Install Python 3 if it is not already available.

DoomCube does not require devkitPPC or libogc2 on the player's computer.


QUICK START
-----------

1. Extract the DoomCube release ZIP.

2. Put at least one legally obtained supported IWAD in:

     WADs/

   Common filenames:

     doom1.wad      DOOM Shareware
     doom.wad       DOOM / The Ultimate DOOM
     doom2.wad      DOOM II
     tnt.wad        Final DOOM: TNT - Evilution
     plutonia.wad   Final DOOM: The Plutonia Experiment

3. Optional additional content:

     PWADs/   custom WADs
     DEH/     DeHackEd / BEX patches

4. Build the native GameCube image:

   Windows:
     build.bat

   Linux / macOS:
     ./build.sh

5. Copy the generated DoomCube .iso to storage accessible from Swiss.

6. Launch the .iso from Swiss.


GAME DATA
---------

Commercial DOOM game data is NOT included with DoomCube.

You must supply your own legally obtained IWAD files.

If no supported IWAD is present, the packer may offer to download and
cryptographically verify DOOM Shareware v1.9.


PLAYER RELEASE CONTENTS
-----------------------

The files under runtime/ are supplied by DoomCube and normally do not need
to be modified.

The release packer combines the prebuilt DoomCube runtime with your WADs
and creates a native GameCube GCM/FST .iso image.


HARDWARE VALIDATION
-------------------

DoomCube has been verified on an original Nintendo GameCube using:

  PicoLoader -> Swiss -> SD Gecko -> native DoomCube ISO

Verified functionality includes Swiss presentation, the DoomCube launcher,
IWAD detection, image-backed game-data reads, audio, GameCube controller
input, and actual DOOM II gameplay.

This validation specifically covers the hardware path above. It does not
claim validation of burned optical discs, other ODEs/loaders, other Swiss
storage devices, or direct-DOL launching.


PROJECT
-------

DoomCube:
  https://github.com/Gothic-Gargoyle/doomcube

CarryHandle:
  https://github.com/Gothic-Gargoyle/carryhandle


LEGAL
-----

DoomCube does not distribute commercial DOOM IWADs.

DOOM and related names and assets are trademarks and copyrighted material
of their respective owners.

Nintendo and GameCube are trademarks of Nintendo.

DoomCube is an independent homebrew project and is not affiliated with or
endorsed by id Software, Bethesda, ZeniMax, or Nintendo.
