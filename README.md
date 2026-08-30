<p align="center">
  <img src="data/launcher/doomcube.bmp" alt="DoomCube" width="200">
</p>

# DoomCube

**DOOM on the Nintendo GameCube.**

DoomCube is a Nintendo GameCube port of DOOM based on DoomGeneric and built
with devkitPPC and libogc2.

The project aims to behave like a proper GameCube port rather than simply a
desktop build moved onto the console. GameCube-specific functionality includes
controller support, rumble, Memory Card saving, a controller-driven launcher,
native GameCube disc images, and a self-contained player packaging workflow.

## Status

DoomCube **v1.0.0** is the first player-facing release.

Experimental true widescreen support is intentionally **not included in
v1.0.0**. That work has been postponed in favour of shipping the stable first
release.

## Screenshot

![DoomCube running in Dolphin](screenshots/dolphin.png)

## Controls

| Action | Control |
| --- | --- |
| Forward | Left analog stick up |
| Backpedal | Left analog stick down |
| Strafe left | Left analog stick left |
| Strafe right | Left analog stick right |
| Turn left | C-stick left |
| Turn right | C-stick right |
| Run | L trigger |
| Fire | R trigger |
| Use | A |
| Back | B |
| Next weapon | X |
| Previous weapon | Y |
| Map | Z |

Controls can be customised in the in-game options menu.

## Features

- Native Nintendo GameCube `.dol`
- Native GameCube GCM/FST disc images
- GameCube-native launcher
- GameCube controller support
- GameCube controller rumble
- Memory Card A save support (16 blocks initially, grows up to 64 blocks)
- Persistent global configuration
- Transactional Save System v3
- Per-game save identities
- MUS/MIDI playback through SDL2_mixer and TiMidity
- Bundled TiMidity instrument set in player releases
- IWAD discovery
- PWAD packaging and launcher discovery
- Self-contained player-facing ISO builder
- No devkitPPC/libogc2 installation required for players
- Classic 4:3 presentation

## Supported games

DoomCube currently recognises these standard IWAD filenames:

| Game | IWAD |
| --- | --- |
| DOOM Shareware | `doom1.wad` |
| DOOM / The Ultimate DOOM | `doom.wad` |
| DOOM II | `doom2.wad` |
| Final DOOM: TNT - Evilution | `tnt.wad` |
| Final DOOM: The Plutonia Experiment | `plutonia.wad` |

**IWADs are not distributed with DoomCube.**

You must supply your own legally obtained DOOM data files.

SIGIL is supported as Episode 5 and SIGIL II as Episode 6. SIGIL compatibility
WADs using Episode 3 are also supported.


## Player release

The player-facing release ZIP contains the DoomCube runtime and everything
required to generate a GameCube disc image, except the DOOM data files
themselves.

A release extracts roughly as follows:

```text
DoomCube/
├── WADs/
├── PWADs/
├── DEH/
├── build.sh
├── build.bat
├── pack.py
└── runtime/
    ├── doomcube.dol
    ├── apploader.bin
    ├── mkdoomcube.py
    ├── launcher/
    └── timidity/
```

Files below `runtime/` are supplied by DoomCube and normally do not need to be
modified by the player.

### Building a disc image

1. Extract the DoomCube release.
2. Copy at least one supported IWAD into `WADs/`.
3. Optionally place additional WADs in `PWADs/`.
4. Optionally place DeHackEd patches in `DEH/`.
5. Run the bundled packer.

On Linux or another system with Python 3:

```bash
python3 pack.py
```

A convenience wrapper is also included:

```bash
./build.sh
```

On Windows:

```text
build.bat
```

The packer combines the supplied WADs with the bundled DoomCube runtime and
creates a native GameCube disc image.

Players do **not** need devkitPPC or libogc2 to use the release packer.

If no supported IWAD is present, the builder can optionally download and
verify DOOM Shareware v1.9.

## Hardware status

DoomCube has not yet been validated on original GameCube hardware.

The v1.0.0 release should therefore still be considered emulator-tested until
it has received an original-hardware test pass.

Saving requires a **Memory Card 251 or larger**.

A new DoomCube Save System v3 container starts at **16 blocks** and can grow up
to **64 blocks** as additional save data is stored.

Save System v3 uses transactional redundant containers, recovery,
copy-on-write updates, container growth and compaction. Cards that are too
small for the storage policy are rejected rather than partially initialised.

## Development documentation

For detailed source-build setup and development workflow, see:

**[DEVELOPMENT.md](DEVELOPMENT.md)**

For implementation details including the native FST runtime, PWAD merge path,
SIGIL episode handling, renderer details and regression infrastructure, see:

**[TECHNICAL.md](TECHNICAL.md)**

## Future work

v1.0.0 prioritises a stable first GameCube release over additional renderer
features.

Planned or experimental work includes:

- revisit true Hor+ widescreen support
- continue improving GameCube-specific platform integration
- continue extracting reusable GameCube functionality

The reusable platform work derived from DoomCube is being developed separately
as **CarryHandle**.

## Legal

DoomCube does not distribute DOOM IWADs.

DOOM and related names and assets are trademarks and copyrighted material of
their respective owners.

Nintendo and GameCube are trademarks of Nintendo.

DoomCube is an independent homebrew project and is not affiliated with or
endorsed by id Software, Bethesda, ZeniMax or Nintendo.

See the repository's licensing information and the notices accompanying
bundled third-party components for their respective terms.
