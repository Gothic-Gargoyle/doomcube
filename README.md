# DoomCube
Since there wasn't a version of DOOM on the Gamecube with access to the source, i decided to fill in this gap! 

To try it you will need a WAD file (game data) on your If you don't own the game, shareware version is freely available (doom1.wad).

# Building

This makes use of devkitpro and libogc2

## Changing over to libogc2
After installing devkitpro as per [the instructions](https://devkitpro.org/wiki/Getting_Started) add [the following lines](https://github.com/extremscorner/pacman-packages) to your /opt/devkitpro/pacman/etc/pacman.conf above above `[dkp-libs]` and `[dkp-linux]`:
```
[libogc2-devkitpro]
Server = https://packages.libogc2.org/devkitpro/linux/$arch
Server = https://packages.extremscorner.org/devkitpro/linux/$arch
```
then enter `sudo dkp-pacman -Syuu` to update to make libogc2 the default.

then install the needed libraries:
`sudo dkp-pacman -S  libogc2 libogc2-libdvm libogc2-physfs libogc2-sdl2 libogc2-sdl2_gfx libogc2-sdl2_image-full libogc2-sdl2_mixer-full libogc2-sdl2_ttf `


# Running this homebrew
## Dolphin


## Real hardware


# Controls
Default:
* Forward = Left analog stick up
* Strafe Left = Left analog stick left
* Strafe Right = Left analog stick right
* backpedal = Left analog stick down
* Run = Left trigger
* Fire = Right trigger
* Use = A button
* Turn Left = C stick

# Features
## Must have
* genericdoom running on gamecube
* sound effects and music

## Should have

## Could have
* Gameboy cable support for displaying map on gameboy

## Wont have

## Dolphin
![Dolphin](screenshots/dolphin.png)

## Real Gamecube
![Real Gamecube](screenshots/Real Gamecube.png)
