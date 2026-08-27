# DoomCube Development Setup

This guide is for getting a DoomCube development environment running,
building the project, and using the developer test commands.

For implementation details, architecture, renderer limits, FST behaviour, and
the regression transport itself, see [TECHNICAL.md](TECHNICAL.md).

# Toolchain and dependencies

DoomCube uses:

- devkitPPC
- libogc2
- SDL2
- SDL2_mixer
- Python 3
- cubeboot-tools

## devkitPro and libogc2

Install devkitPro according to the official Getting Started guide:

https://devkitpro.org/wiki/Getting_Started

Then add the libogc2 repositories described here:

https://github.com/extremscorner/pacman-packages

Edit:

```text
/opt/devkitpro/pacman/etc/pacman.conf
```

and add the following above `[dkp-libs]` and `[dkp-linux]`:

```ini
[libogc2-devkitpro]
Server = https://packages.libogc2.org/devkitpro/linux/$arch
Server = https://packages.extremscorner.org/devkitpro/linux/$arch
```

Update the devkitPro packages:

```bash
sudo dkp-pacman -Syuu
```

Install the GameCube libraries required by DoomCube:

```bash
sudo dkp-pacman -S \
    libogc2 \
    libogc2-sdl2 \
    libogc2-sdl2_mixer-full
```

On Debian/Ubuntu, also install Python 3, Git, and wget:

```bash
sudo apt install python3 git wget
```

# cubeboot-tools

DoomCube uses parts of Extrems' cubeboot-tools to build its GameCube
apploader.

Clone it into `build-deps`:

```bash
mkdir -p build-deps

git clone \
    https://github.com/extremscorner/cubeboot-tools.git \
    build-deps/cubeboot-tools
```

The `build-deps/` directory is intentionally ignored by Git.

DoomCube's modified apploader source is tracked separately at:

```text
tools/native-gcm/apploader.c
```

When an image or release is built, DoomCube copies this source into the
cubeboot-tools build directory, builds `apploader.bin`, and uses it when
constructing the GameCube image.

# Source-tree data

## IWADs

Place development IWADs in:

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

Only IWADs present when the image is built are included.

Commercial IWADs should never be committed to the repository or included in
release archives.

## PWADs

Place development PWADs in:

```text
data/pwad/
```

The image builder scans this directory and generates the launcher manifest
automatically.

## DeHackEd patches

Optional development DeHackEd patches go in:

```text
data/deh/
```

## TiMidity runtime data

DoomCube plays converted MIDI through SDL2_mixer's TiMidity backend.

Developers building from source need the General MIDI instrument data in:

```text
data/timidity/
```

Install it with:

```bash
mkdir -p data/timidity
cd data/timidity

wget https://www.libsdl.org/projects/old/SDL_mixer/timidity/timidity.tar.gz
tar -xzf timidity.tar.gz

cp -a timidity/. .

rm -rf timidity
rm timidity.tar.gz

sed -i '4i dir dvd:/data/timidity\n' timidity.cfg

cd ../..
```

Verify the runtime path:

```bash
grep -n '^dir ' data/timidity/timidity.cfg
```

Expected entry:

```text
dir dvd:/data/timidity
```

The instrument set should contain 192 `.pat` files:

```bash
find data/timidity -iname '*.pat' | wc -l
```

Expected output:

```text
192
```

# Build targets

## Build the DOL

```bash
make
```

Creates a versioned executable similar to:

```text
doomcube-v0.1.0-dev-xxxxxxxx.dol
```

The Git commit hash is included in the filename. A modified worktree adds
`-dirty` to the build ID.

## Build the native GameCube image

```bash
make iso
```

Creates a versioned native GameCube disc image, for example:

```text
doomcube-v0.1.0-dev-xxxxxxxx.iso
```

## Build and launch in Dolphin

```bash
make test
```

This performs a clean rebuild, creates the native image, and launches it using
the Flatpak version of Dolphin.

## Release targets

```text
make test-rc
make rc
make release
make release-debug
```

`make release` creates a self-contained player release under `dist/`.

Release archives intentionally contain no commercial IWADs.

## Clean

```bash
make clean
```

# Developer test harness

## Launcher autoselection and warp

Development builds can automatically select an IWAD/PWAD and warp to a map.

Example:

```bash
make test \
    TEST_IWAD=doom.wad \
    TEST_PWAD=SIGIL_V1_23.wad \
    WARP_EPISODE=5 \
    WARP_MAP=6
```

`TEST_PWAD` is optional but requires `TEST_IWAD`.

`WARP_EPISODE` and `WARP_MAP` must be supplied together.

When a warp is supplied, DoomCube automatically exits the initial test map
after a short delay. SIGIL secret-entry maps use the appropriate secret exit.

## SIGIL compatibility stress test

Place:

```text
SIGIL_COMPAT_V1_23.wad
```

in:

```text
data/pwad/
```

Then run:

```bash
./tools/test-sigil.sh
```

Specific maps may also be supplied:

```bash
./tools/test-sigil.sh 1 5 9
```

# Automated regression

Run the complete regression suite with:

```bash
make regression
```

The suite requires:

```text
data/wad/doom.wad
data/pwad/SIGIL_V1_23.wad
data/pwad/SIGIL_II_V1_0.WAD
```

and the Flatpak version of Dolphin:

```text
org.DolphinEmu.dolphin-emu
```

Do not run an unrelated Flatpak Dolphin session while the suite is active.

For how the single-artifact regression framework, RTC case transport, and
runtime markers work, see [TECHNICAL.md](TECHNICAL.md).

# Before submitting changes

At minimum:

```bash
git diff --check
make regression
```

For changes that affect release packaging, also exercise the relevant release
target.

Original GameCube hardware still needs broader validation, especially for
Memory Card behaviour, audio, rumble, disc access, and long-running stability.
