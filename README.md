<p align="center">
  <img src="data/launcher/doomcube.bmp" alt="DoomCube" width="200">
</p>

# DoomCube

Since there wasn't a version of DOOM for the Nintendo GameCube with source
code available, I decided to fill that gap!

DoomCube currently supports:

- DOOM Shareware
- The Ultimate DOOM
- DOOM II
- Final DOOM: TNT - Evilution
- Final DOOM: The Plutonia Experiment

To play, you will need the appropriate WADs. If you don't own DOOM, the
shareware version (`doom1.wad`) is freely available.

(SAVE) BEHAVIOUR IS UNTESTED ON REAL HARDWARE, IT MIGHT CORRUPT YOUR SAVES/MEMORY CARD!! ALSO PLEASE NOTE THAT THE MEMORY FILE USES 116 BLOCKS!! 

# Running
## SD card (untested)
You can run `doomcube.dol` from an SD card, WADs go in /data/WAD/ and the music patches ([see here](##Music)) go in /data/timidity, not yet tested on real hardware!!

## ISO
After building this project you can run `doomcube.dol` on dolphin, not yet tested on real hardware!!

# Building

DoomCube uses devkitPPC and libogc2.

## Changing over to libogc2

First install devkitPro according to the
[devkitPro Getting Started guide](https://devkitpro.org/wiki/Getting_Started).

Then add the libogc2 repositories described by
[extremscorner/pacman-packages](https://github.com/extremscorner/pacman-packages)
to:

```
/opt/devkitpro/pacman/etc/pacman.conf
```

by adding the following above `[dkp-libs]` and `[dkp-linux]`:

```
[libogc2-devkitpro]
Server = https://packages.libogc2.org/devkitpro/linux/$arch
Server = https://packages.extremscorner.org/devkitpro/linux/$arch
```

Then update the devkitPro packages:

```bash
sudo dkp-pacman -Syuu
```

Install the libraries required by DoomCube:

```bash
sudo dkp-pacman -S \
    libogc2 \
    libogc2-libdvm \
    libogc2-sdl2 \
    libogc2-sdl2_mixer-full
```

Building an ISO also requires `genisoimage`, `mkisofs`, or `xorriso`.

For example, on Debian/Ubuntu:

```bash
sudo apt install genisoimage
```

## WADs

Place your IWADs in:

```text
data/wad/
```

Supported filenames are:

```text
doom1.wad
doom.wad
doom2.wad
tnt.wad
plutonia.wad
```

Only WADs present when the ISO is built will be included. The DoomCube launcher
automatically detects the games available on the disc.

## Music

DoomCube converts DOOM's MUS music to MIDI at runtime and plays it through
SDL2_mixer's TiMidity backend.

libogc2 provides the TiMidity implementation, but the instrument patches must
be supplied separately. DoomCube does not distribute these patches.

The SDL_mixer TiMidity instrument set can be installed with:

```bash
mkdir -p data/timidity
cd data/timidity

wget https://www.libsdl.org/projects/old/SDL_mixer/timidity/timidity.tar.gz
tar -xzf timidity.tar.gz

cp -a timidity/. .

rm -rf timidity
rm timidity.tar.gz

sed -i '4i dir dvd:/timidity\n' timidity.cfg

cd ../..
```

Verify that the configuration contains:

```text
dir dvd:/timidity
```

and that all 192 patches are present:

```bash
grep -n '^dir ' data/timidity/timidity.cfg
find data/timidity -iname '*.pat' | wc -l
```

The second command should output:

```text
192
```

## Building the ISO

Build DoomCube:

```bash
make
make iso
```

This creates:

```text
doomcube.iso
```

To clean, rebuild, create the ISO, and launch it in the Flatpak version of
Dolphin:

```bash
make test
```

Real GameCube hardware is currently untested.

# Controls

Default controls:

- Forward = Left analog stick up
- Backpedal = Left analog stick down
- Turn left = C-stick left
- Turn right = C-stick right
- Strafe left = Left analog stick left
- Strafe right = Left analog stick right
- Run = L trigger
- Fire = R trigger
- Use = A
- Next weapon = X
- Previous weapon = Y
- Map = Z

Controls can be customised in the in-game options menu.

# Features

## Implemented

- DoomGeneric running natively on the Nintendo GameCube
- DOOM Shareware, Ultimate DOOM, DOOM II, TNT and Plutonia support
- GameCube controller support
- Customisable controls
- Sound effects
- MUS-to-MIDI conversion
- MIDI music through SDL2_mixer/TiMidity
- GameCube controller rumble
- Memory Card save support
- Per-game save handling (1 save per game)
- Multi-IWAD launcher
- DoomCube launcher artwork
- Disc-based IWAD loading

## Known Issues

- MIDI music playback can stop working after DoomCube has been running for some
  time. Individual tracks appear to play correctly, but music may stop during a
  later music transition. The exact cause is currently unknown and under
  investigation.
- Original GameCube hardware has not yet been tested.

## Could Have

- Fully fledged Memory Card save file with GameCube icon/banner metadata
- Improved disc I/O/caching to reduce stuttering
- Game Boy Advance cable support for displaying the automap
- Local four-player split-screen multiplayer
- Online multiplayer

# Screenshots

## Dolphin

![Dolphin](screenshots/dolphin.png)
