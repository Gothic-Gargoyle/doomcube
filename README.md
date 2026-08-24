# DoomCube
Since there wasn't a version of DOOM on the Gamecube with access to the source, i decided to fill in this gap! 

Support for shareware,ultimatedoom, doom2, plutonia and TNT!

To try it you will need a WAD file, If you don't own the game, the shareware version (doom1.wad) is freely available .

# Building

This makes use of libogc2.

## Changing over to libogc2
After installing devkitpro as per [the instructions](https://devkitpro.org/wiki/Getting_Started) add [the following lines](https://github.com/extremscorner/pacman-packages) to `/opt/devkitpro/pacman/etc/pacman.conf` above `[dkp-libs]` and `[dkp-linux]`:
```
[libogc2-devkitpro]
Server = https://packages.libogc2.org/devkitpro/linux/$arch
Server = https://packages.extremscorner.org/devkitpro/linux/$arch
```
then enter `sudo dkp-pacman -Syuu` to update to make libogc2 the default.

then install the needed libraries:
`sudo dkp-pacman -S libogc2 libogc2-libdvm libogc2-sdl2 libogc2-sdl2_gfx libogc2-sdl2_image-full libogc2-sdl2_mixer-full libogc2-sdl2_ttf `

## WAD
Place in /data folder.

## Music
libogc2 exposes Timidity, but youll have to provide patches yourself, the project default has the following:

```
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

 verify that the config has `4:dor dvd:/timidity` and that there are 192 patches.

```
grep -n '^dir ' data/timidity/timidity.cfg
find data/timidity -iname '*.pat' | wc -l
```

Then build the project run `make test`, this will create a `doomcube.iso` that you can play on Dolphin, *OG hardware is as of yet untested*.

# Controls
Default:
* Forward = Left analog stick up
* Backpedal = Left analog stick down
* Turn Left = C stick left
* Turn Right = c stick right
* Strafe Left = Left analog stick left
* Strafe Right = Left analog stick right

* Run = Left trigger
* Fire = Right trigger
* Use = A button
* Next weapon = X
* Previous Weapon = Y

* Map = Z

# Features
## Must have
* genericdoom running on gamecube
* sound effects 
* music

## Should have
* rumble when firing
* Save file support
* customisable controls

## Could have
* Fuflly fledged savefile with icon and whatnot.
* fully fledged midi player
* Better reading of data (WADs get read from disc and this causes stuttering)
* Gameboy cable support for displaying map on gameboy
* Local 4 screen MP
* Online MP

## Wont have

## Dolphin
![Dolphin](screenshots/dolphin.png)

## Real Gamecube
![Real Gamecube](screenshots/Real Gamecube.png)
