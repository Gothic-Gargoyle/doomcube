#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include "doomtype.h"
#include "i_sound.h"
#include "sounds.h"

#include <stdio.h>
#include <string.h>

#ifdef DOOMCUBE_EMBED_ASSETS
#include "d_e1m1_ogg.h"
#endif

static boolean music_initialized;
static boolean owns_audio;
static int current_volume = 127;

static const char *findMusicName(void *data)
{
    int i;

    for (i = 1; i < NUMMUSIC; ++i)
    {
        if (S_music[i].data == data)
            return S_music[i].name;
    }

    return NULL;
}

static boolean GC_MusicInit(void)
{
    int freq;
    int channels;
    Uint16 format;

    if (!(Mix_Init(MIX_INIT_OGG) & MIX_INIT_OGG))
    {
        printf(
            "DoomCube: OGG init failed: %s\n",
            Mix_GetError()
        );

        return false;
    }

    if (!Mix_QuerySpec(&freq, &format, &channels))
    {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
        {
            printf(
                "DoomCube: SDL audio init failed: %s\n",
                SDL_GetError()
            );

            return false;
        }

        if (Mix_OpenAudio(
                snd_samplerate,
                AUDIO_S16SYS,
                2,
                1024
            ) < 0)
        {
            printf(
                "DoomCube: Mix_OpenAudio failed: %s\n",
                Mix_GetError()
            );

            return false;
        }

        owns_audio = true;
    }

    Mix_VolumeMusic(
        (current_volume * MIX_MAX_VOLUME) / 127
    );

    music_initialized = true;

    printf("DoomCube: OGG music ready\n");

    return true;
}

static void GC_MusicShutdown(void)
{
    if (!music_initialized)
        return;

    Mix_HaltMusic();
    Mix_Quit();

    if (owns_audio)
    {
        Mix_CloseAudio();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    music_initialized = false;
}

static void GC_SetMusicVolume(int volume)
{
    current_volume = volume;

    if (music_initialized)
    {
        Mix_VolumeMusic(
            (volume * MIX_MAX_VOLUME) / 127
        );
    }
}

static void GC_PauseMusic(void)
{
    if (music_initialized)
        Mix_PauseMusic();
}

static void GC_ResumeMusic(void)
{
    if (music_initialized)
        Mix_ResumeMusic();
}

static void *GC_RegisterSong(void *data, int len)
{
    const char *name;

    (void)len;

    if (!music_initialized)
        return NULL;

    name = findMusicName(data);

    if (!name)
    {
        printf("DoomCube: unknown music lump\n");
        return NULL;
    }

#ifdef DOOMCUBE_EMBED_ASSETS

    /*
     * Dolphin test build:
     * embed only E1M1 to keep the DOL small.
     */
    if (!strcmp(name, "e1m1"))
    {
        SDL_RWops *rw;
        Mix_Music *music;

        printf(
            "DoomCube: loading embedded d_e1m1.ogg\n"
        );

        rw = SDL_RWFromConstMem(
            d_e1m1_ogg,
            d_e1m1_ogg_size
        );

        if (!rw)
        {
            printf(
                "DoomCube: SDL_RWFromConstMem failed: %s\n",
                SDL_GetError()
            );

            return NULL;
        }

        music = Mix_LoadMUS_RW(
            rw,
            1
        );

        if (!music)
        {
            printf(
                "DoomCube: Mix_LoadMUS_RW failed: %s\n",
                Mix_GetError()
            );
        }

        return music;
    }

    printf(
        "DoomCube: music '%s' not embedded\n",
        name
    );

    return NULL;

#else

    /*
     * Real GameCube build:
     * load OGGs from filesystem.
     */
    char path[64];
    Mix_Music *music;

    snprintf(
        path,
        sizeof(path),
        "music/d_%s.ogg",
        name
    );

    printf(
        "DoomCube: loading %s\n",
        path
    );

    music = Mix_LoadMUS(path);

    if (!music)
    {
        printf(
            "DoomCube: failed loading %s: %s\n",
            path,
            Mix_GetError()
        );
    }

    return music;

#endif
}

static void GC_UnRegisterSong(void *handle)
{
    if (handle)
        Mix_FreeMusic((Mix_Music *)handle);
}

static void GC_PlaySong(
    void *handle,
    boolean looping)
{
    if (!music_initialized || !handle)
        return;

    if (Mix_PlayMusic(
            (Mix_Music *)handle,
            looping ? -1 : 0
        ) < 0)
    {
        printf(
            "DoomCube: Mix_PlayMusic failed: %s\n",
            Mix_GetError()
        );
    }
}

static void GC_StopSong(void)
{
    if (music_initialized)
        Mix_HaltMusic();
}

static boolean GC_MusicIsPlaying(void)
{
    if (!music_initialized)
        return false;

    return Mix_PlayingMusic() != 0;
}

static void GC_MusicPoll(void)
{
}

static snddevice_t gc_music_devices[] =
{
    SNDDEVICE_SB,
    SNDDEVICE_PAS,
    SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS,
    SNDDEVICE_GENMIDI,
    SNDDEVICE_AWE32
};

music_module_t DG_music_module =
{
    gc_music_devices,
    sizeof(gc_music_devices)
        / sizeof(gc_music_devices[0]),

    GC_MusicInit,
    GC_MusicShutdown,
    GC_SetMusicVolume,
    GC_PauseMusic,
    GC_ResumeMusic,
    GC_RegisterSong,
    GC_UnRegisterSong,
    GC_PlaySong,
    GC_StopSong,
    GC_MusicIsPlaying,
    GC_MusicPoll
};