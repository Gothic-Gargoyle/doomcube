#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include <ogc/system.h>

#include "gc_debug.h"

#include "doomtype.h"
#include "i_sound.h"
#include "memio.h"
#include "mus2mid.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define GC_TIMIDITY_CFG "dvd:/data/timidity/timidity.cfg"


typedef struct
{
    Mix_Music *music;

    /*
     * Keep the generated MIDI data alive for the lifetime of Mix_Music.
     *
     * TiMidity may continue reading from the RWops source after
     * Mix_LoadMUS_RW(), so the backing memory must not disappear early.
     */
    void *midi_data;
    size_t midi_len;

} gc_music_handle_t;


static boolean music_initialized;
static boolean owns_audio;
static int current_volume = 127;


/* ------------------------------------------------------------------------- */
/* Init                                                                      */
/* ------------------------------------------------------------------------- */

static boolean GC_MusicInit(void)
{
    int freq;
    int channels;
    Uint16 format;
    const char *cfg;

    DC_DEBUG(
        "DoomCube: initializing music\n"
    );

    /*
     * TiMidity is built into libogc2's SDL2_mixer.
     * MIDI does not require a Mix_Init codec flag.
     */
    Mix_Init(0);

    if (!Mix_QuerySpec(
            &freq,
            &format,
            &channels))
    {
        DC_DEBUG(
            "DoomCube: SDL_mixer audio not open; opening it\n"
        );

        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
        {
            DC_WARN(
                "DoomCube: SDL audio init failed: %s\n",
                SDL_GetError()
            );

            Mix_Quit();

            return false;
        }

        if (Mix_OpenAudio(
                snd_samplerate,
                AUDIO_S16SYS,
                2,
                1024) < 0)
        {
            DC_WARN(
                "DoomCube: Mix_OpenAudio failed: %s\n",
                Mix_GetError()
            );

            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            Mix_Quit();

            return false;
        }

        owns_audio = true;
    }
    else
    {
        DC_DEBUG(
            "DoomCube: SDL_mixer already open: "
            "freq=%d channels=%d format=0x%x\n",
            freq,
            channels,
            (unsigned int)format
        );
    }

    /*
     * SDL_mixer normally looks for /etc/timidity.cfg.
     * Point it at the copy on the GameCube DVD.
     *
     * Mix_SetTimidityCfg returns 1 on success, 0 on failure.
     */
    if (!Mix_SetTimidityCfg(GC_TIMIDITY_CFG))
    {
        DC_WARN(
            "DoomCube: Mix_SetTimidityCfg failed: %s\n",
            Mix_GetError()
        );

        if (owns_audio)
        {
            Mix_CloseAudio();
            SDL_QuitSubSystem(SDL_INIT_AUDIO);

            owns_audio = false;
        }

        Mix_Quit();

        return false;
    }

    cfg = Mix_GetTimidityCfg();

    DC_DEBUG(
        "DoomCube: TiMidity config: %s\n",
        cfg != NULL ? cfg : "(null)"
    );

    Mix_VolumeMusic(
        (current_volume * MIX_MAX_VOLUME) / 127
    );

    music_initialized = true;

    DC_INFO(
        "DoomCube: MUS/MIDI TiMidity backend ready\n"
    );

    return true;
}


/* ------------------------------------------------------------------------- */
/* Shutdown                                                                  */
/* ------------------------------------------------------------------------- */

static void GC_MusicShutdown(void)
{
    DC_DEBUG(
        "DoomCube: GC_MusicShutdown initialized=%d playing=%d\n",
        music_initialized,
        Mix_PlayingMusic()
    );

    if (!music_initialized)
        return;

    Mix_HaltMusic();

    if (owns_audio)
    {
        DC_DEBUG(
            "DoomCube: closing owned SDL_mixer audio device\n"
        );

        Mix_CloseAudio();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);

        owns_audio = false;
    }

    Mix_Quit();

    music_initialized = false;

    DC_DEBUG(
        "DoomCube: music backend shut down\n"
    );
}


/* ------------------------------------------------------------------------- */
/* Volume                                                                    */
/* ------------------------------------------------------------------------- */

static void GC_SetMusicVolume(int volume)
{
    if (volume < 0)
        volume = 0;

    if (volume > 127)
        volume = 127;

    current_volume = volume;

    if (music_initialized)
    {
        Mix_VolumeMusic(
            (current_volume * MIX_MAX_VOLUME) / 127
        );
    }
}


/* ------------------------------------------------------------------------- */
/* Pause / resume                                                            */
/* ------------------------------------------------------------------------- */

static void GC_PauseMusic(void)
{
    DC_TRACE(
        "DoomCube: PauseMusic playing=%d paused=%d\n",
        Mix_PlayingMusic(),
        Mix_PausedMusic()
    );

    if (music_initialized &&
        Mix_PlayingMusic())
    {
        Mix_PauseMusic();
    }
}


static void GC_ResumeMusic(void)
{
    DC_TRACE(
        "DoomCube: ResumeMusic playing=%d paused=%d\n",
        Mix_PlayingMusic(),
        Mix_PausedMusic()
    );

    if (music_initialized &&
        Mix_PausedMusic())
    {
        Mix_ResumeMusic();
    }
}


/* ------------------------------------------------------------------------- */
/* MIDI helpers                                                              */
/* ------------------------------------------------------------------------- */

static boolean GC_IsMidi(
    const void *data,
    int len)
{
    return data != NULL &&
           len >= 4 &&
           memcmp(data, "MThd", 4) == 0;
}


static boolean GC_ConvertMusToMidi(
    const void *data,
    int len,
    void **midi_data,
    size_t *midi_len)
{
    MEMFILE *input;
    MEMFILE *output;

    void *buffer;
    size_t buffer_len;

    void *copy;

    *midi_data = NULL;
    *midi_len = 0;

    input =
        mem_fopen_read(
            (void *)data,
            (size_t)len
        );

    if (input == NULL)
    {
        DC_WARN(
            "DoomCube: mem_fopen_read failed\n"
        );

        return false;
    }

    output =
        mem_fopen_write();

    if (output == NULL)
    {
        DC_WARN(
            "DoomCube: mem_fopen_write failed\n"
        );

        mem_fclose(input);

        return false;
    }

    /*
     * mus2mid() returns false/0 on success and true/1 on failure.
     */
    if (mus2mid(
            input,
            output))
    {
        DC_WARN(
            "DoomCube: MUS -> MIDI conversion FAILED\n"
        );

        mem_fclose(output);
        mem_fclose(input);

        return false;
    }

    buffer = NULL;
    buffer_len = 0;

    mem_get_buf(
        output,
        &buffer,
        &buffer_len
    );

    if (buffer == NULL ||
        buffer_len == 0)
    {
        DC_WARN(
            "DoomCube: MUS -> MIDI produced empty output\n"
        );

        mem_fclose(output);
        mem_fclose(input);

        return false;
    }

    copy =
        malloc(buffer_len);

    if (copy == NULL)
    {
        DC_WARN(
            "DoomCube: failed allocating MIDI buffer: %lu bytes\n",
            (unsigned long)buffer_len
        );

        mem_fclose(output);
        mem_fclose(input);

        return false;
    }

    memcpy(
        copy,
        buffer,
        buffer_len
    );

    mem_fclose(output);
    mem_fclose(input);

    *midi_data = copy;
    *midi_len = buffer_len;

    return true;
}

/* ------------------------------------------------------------------------- */
/* Register                                                                  */
/* ------------------------------------------------------------------------- */

static void *GC_RegisterSong(
    void *data,
    int len)
{
    gc_music_handle_t *handle;
    SDL_RWops *rw;

    void *midi_data;
    size_t midi_len;

    DC_TRACE(
        "DoomCube: RegisterSong data=%p len=%d\n",
        data,
        len
    );

    if (!music_initialized ||
        data == NULL ||
        len <= 0)
    {
        DC_WARN(
            "DoomCube: RegisterSong rejected "
            "initialized=%d data=%p len=%d\n",
            music_initialized,
            data,
            len
        );

        return NULL;
    }

    handle =
        calloc(
            1,
            sizeof(*handle)
        );

    if (handle == NULL)
    {
        DC_WARN(
            "DoomCube: failed allocating music handle\n"
        );

        return NULL;
    }

    if (GC_IsMidi(data, len))
    {
        DC_TRACE(
            "DoomCube: source lump is already MIDI (%d bytes)\n",
            len
        );

        handle->midi_data =
            malloc((size_t)len);

        if (handle->midi_data == NULL)
        {
            DC_WARN(
                "DoomCube: failed allocating direct MIDI buffer\n"
            );

            free(handle);

            return NULL;
        }

        memcpy(
            handle->midi_data,
            data,
            (size_t)len
        );

        handle->midi_len =
            (size_t)len;
    }
    else
    {
        DC_TRACE(
            "DoomCube: converting MUS (%d bytes)\n",
            len
        );

        midi_data = NULL;
        midi_len = 0;

        if (!GC_ConvertMusToMidi(
                data,
                len,
                &midi_data,
                &midi_len))
        {
            DC_WARN(
                "DoomCube: conversion FAILED\n"
            );

            free(handle);

            return NULL;
        }

        handle->midi_data = midi_data;
        handle->midi_len = midi_len;

        DC_TRACE(
            "DoomCube: conversion OK: %lu MIDI bytes\n",
            (unsigned long)midi_len
        );
    }

    if (handle->midi_len > (size_t)INT_MAX)
    {
        DC_WARN(
            "DoomCube: MIDI lump too large: %lu bytes\n",
            (unsigned long)handle->midi_len
        );

        free(handle->midi_data);
        free(handle);

        return NULL;
    }

    rw =
        SDL_RWFromConstMem(
            handle->midi_data,
            (int)handle->midi_len
        );

    if (rw == NULL)
    {
        DC_WARN(
            "DoomCube: SDL_RWFromConstMem FAILED: %s\n",
            SDL_GetError()
        );

        free(handle->midi_data);
        free(handle);

        return NULL;
    }

    SDL_ClearError();

    handle->music =
        Mix_LoadMUS_RW(
            rw,
            1
        );

    DC_TRACE(
        "DoomCube: Mix_LoadMUS_RW => %p error='%s'\n",
        (void *)handle->music,
        Mix_GetError()
    );

    if (handle->music == NULL)
    {
        DC_WARN(
            "DoomCube: Mix_LoadMUS_RW FAILED\n"
        );

        free(handle->midi_data);
        free(handle);

        return NULL;
    }

    DC_TRACE(
        "DoomCube: RegisterSong => handle=%p music=%p "
        "midi=%p len=%lu\n",
        (void *)handle,
        (void *)handle->music,
        handle->midi_data,
        (unsigned long)handle->midi_len
    );

    return handle;
}


/* ------------------------------------------------------------------------- */
/* Unregister                                                                */
/* ------------------------------------------------------------------------- */

static void GC_UnRegisterSong(void *handle_ptr)
{
    gc_music_handle_t *handle =
        (gc_music_handle_t *)handle_ptr;

    DC_TRACE(
        "DoomCube: UnRegisterSong handle=%p "
        "playing=%d paused=%d\n",
        handle_ptr,
        Mix_PlayingMusic(),
        Mix_PausedMusic()
    );

    if (handle == NULL)
    {
        DC_TRACE(
            "DoomCube: UnRegisterSong NULL handle\n"
        );

        return;
    }

    /*
     * Defensive halt:
     *
     * Do not let SDL_mixer/TiMidity continue touching the Mix_Music
     * object or its backing MIDI memory while we destroy it.
     */
    Mix_HaltMusic();

    DC_TRACE(
        "DoomCube: after defensive halt playing=%d\n",
        Mix_PlayingMusic()
    );

    if (handle->music != NULL)
    {
        DC_TRACE(
            "DoomCube: freeing Mix_Music %p\n",
            (void *)handle->music
        );

        Mix_FreeMusic(
            handle->music
        );

        handle->music = NULL;

        DC_TRACE(
            "DoomCube: Mix_Music freed\n"
        );
    }

    DC_TRACE(
        "DoomCube: freeing MIDI buffer %p (%lu bytes)\n",
        handle->midi_data,
        (unsigned long)handle->midi_len
    );

    free(handle->midi_data);

    handle->midi_data = NULL;
    handle->midi_len = 0;

    free(handle);

    DC_TRACE(
        "DoomCube: UnRegisterSong complete\n"
    );
}


/* ------------------------------------------------------------------------- */
/* Playback                                                                  */
/* ------------------------------------------------------------------------- */

static void GC_PlaySong(
    void *handle_ptr,
    boolean looping)
{
    gc_music_handle_t *handle =
        (gc_music_handle_t *)handle_ptr;

    DC_TRACE(
        "DoomCube: PlaySong handle=%p looping=%d\n",
        handle_ptr,
        looping
    );

    if (!music_initialized)
    {
        DC_WARN(
            "DoomCube: PlaySong rejected: backend not initialized\n"
        );

        return;
    }

    if (handle == NULL)
    {
        DC_WARN(
            "DoomCube: PlaySong rejected: NULL handle\n"
        );

        return;
    }

    if (handle->music == NULL)
    {
        DC_WARN(
            "DoomCube: PlaySong rejected: NULL Mix_Music\n"
        );

        return;
    }

    SDL_ClearError();

    DC_TRACE(
        "DoomCube: Mix_PlayMusic music=%p\n",
        (void *)handle->music
    );

    if (Mix_PlayMusic(
            handle->music,
            looping ? -1 : 0) < 0)
    {
        DC_WARN(
            "DoomCube: Mix_PlayMusic FAILED: %s\n",
            Mix_GetError()
        );

        return;
    }

    DC_TRACE(
        "DoomCube: Mix_PlayMusic OK "
        "playing=%d paused=%d\n",
        Mix_PlayingMusic(),
        Mix_PausedMusic()
    );
}


static void GC_StopSong(void)
{
    DC_TRACE(
        "DoomCube: StopSong before halt "
        "playing=%d paused=%d\n",
        Mix_PlayingMusic(),
        Mix_PausedMusic()
    );

    if (music_initialized)
    {
        Mix_HaltMusic();
    }

    DC_TRACE(
        "DoomCube: StopSong after halt "
        "playing=%d paused=%d\n",
        Mix_PlayingMusic(),
        Mix_PausedMusic()
    );
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


/* ------------------------------------------------------------------------- */
/* Module                                                                    */
/* ------------------------------------------------------------------------- */

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