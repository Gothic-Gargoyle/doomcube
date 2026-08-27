<p align="center">
  <img src="data/launcher/doomcube.bmp" alt="DoomCube" width="200">
</p>

# DoomCube

DoomCube is a native Nintendo GameCube port of DoomGeneric with GameCube
controls, rumble, audio, Memory Card saves, a launcher, PWAD support, and
native GameCube disc-image support.

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

## Supported games

- DOOM Shareware
- The Ultimate DOOM
- DOOM II
- Final DOOM: TNT - Evilution
- Final DOOM: The Plutonia Experiment

Commercial IWADs are not included with DoomCube.

SIGIL is supported as Episode 5 and SIGIL II as Episode 6. SIGIL compatibility
WADs using Episode 3 are also supported.

## Running

Use a DoomCube release package.

Place your files in:

```text
WADs/   - IWADs
PWADs/  - optional PWADs
DEH/    - optional DeHackEd patches
```

Then build the GameCube image:

### Linux

```bash
./build.sh
```
### Windows
```bat
build.bat
```

If no supported IWAD is present, `build.py`/`build.bat` can optionally download and verify
DOOM Shareware v1.9.

A successful build produces:

```text
DoomCube.iso
```

Open the image in Dolphin or use it with a compatible GameCube homebrew loader
or optical-drive replacement solution.

DoomCube currently supports one PWAD per launch.

## Hardware status

DoomCube has not yet been validated on original GameCube hardware.

Memory Card saving should therefore still be treated as experimental on real
hardware. The current save container uses approximately 116 blocks.

## Development

To get a source build running, see:

**[DEVELOPMENT.md](DEVELOPMENT.md)**

For implementation details such as the native FST runtime, PWAD merge path,
SIGIL episode handling, renderer limits, and RTC-driven regression transport,
see:

**[TECHNICAL.md](TECHNICAL.md)**
