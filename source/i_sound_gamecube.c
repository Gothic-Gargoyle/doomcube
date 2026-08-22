#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>

#include "deh_str.h"
#include "doomtype.h"
#include "i_sound.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NUM_CHANNELS 16

typedef struct
{
    const uint8_t *data;
    uint32_t length;

    uint32_t position;
    uint32_t step;

    int left;
    int right;

    boolean active;
} gc_channel_t;

static SDL_AudioDeviceID audio_device;
static SDL_AudioSpec audio_spec;

static gc_channel_t channels[NUM_CHANNELS];

static boolean sound_initialized;
static boolean use_sfx_prefix;

/*
 * i_sound.c expects these when FEATURE_SOUND is enabled.
 * We don't use libsamplerate on GameCube.
 */
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;


static int clamp16(int sample)
{
    if (sample > 32767)
        return 32767;

    if (sample < -32768)
        return -32768;

    return sample;
}


static void audioCallback(void *userdata, Uint8 *stream, int len)
{
    Sint16 *output = (Sint16 *)stream;
    int frames = len / (sizeof(Sint16) * 2);

    (void)userdata;

    memset(stream, 0, len);

    for (int frame = 0; frame < frames; ++frame)
    {
        int mixLeft = 0;
        int mixRight = 0;

        for (int ch = 0; ch < NUM_CHANNELS; ++ch)
        {
            gc_channel_t *channel = &channels[ch];

            if (!channel->active)
                continue;

            uint32_t sampleIndex = channel->position >> 16;

            if (sampleIndex >= channel->length)
            {
                channel->active = false;
                continue;
            }

            int sample =
                ((int)channel->data[sampleIndex] - 128) << 8;

            mixLeft +=
                (sample * channel->left) / 255;

            mixRight +=
                (sample * channel->right) / 255;

            channel->position += channel->step;
        }

        output[frame * 2] =
            (Sint16)clamp16(mixLeft);

        output[frame * 2 + 1] =
            (Sint16)clamp16(mixRight);
    }
}


static void getSfxLumpName(
    sfxinfo_t *sfx,
    char *buffer,
    size_t bufferSize)
{
    if (sfx->link != NULL)
        sfx = sfx->link;

    if (use_sfx_prefix)
    {
        M_snprintf(
            buffer,
            bufferSize,
            "ds%s",
            DEH_String(sfx->name)
        );
    }
    else
    {
        M_StringCopy(
            buffer,
            DEH_String(sfx->name),
            bufferSize
        );
    }
}


static int GC_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char name[9];

    getSfxLumpName(
        sfx,
        name,
        sizeof(name)
    );

    return W_GetNumForName(name);
}


static boolean GC_InitSound(boolean prefix)
{
    SDL_AudioSpec desired;

    use_sfx_prefix = prefix;

    memset(channels, 0, sizeof(channels));
    memset(&desired, 0, sizeof(desired));

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
    {
        printf(
            "DoomCube: SDL audio init failed: %s\n",
            SDL_GetError()
        );

        return false;
    }

    desired.freq = 48000;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = audioCallback;

    audio_device = SDL_OpenAudioDevice(
        NULL,
        0,
        &desired,
        &audio_spec,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE
    );

    if (audio_device == 0)
    {
        printf(
            "DoomCube: SDL_OpenAudioDevice failed: %s\n",
            SDL_GetError()
        );

        return false;
    }

    printf(
        "DoomCube: audio %d Hz, %d channels\n",
        audio_spec.freq,
        audio_spec.channels
    );

    sound_initialized = true;

    SDL_PauseAudioDevice(
        audio_device,
        0
    );

    return true;
}


static void GC_ShutdownSound(void)
{
    if (!sound_initialized)
        return;

    SDL_CloseAudioDevice(audio_device);

    audio_device = 0;
    sound_initialized = false;
}


static void GC_UpdateSound(void)
{
}


static void GC_UpdateSoundParams(
    int channel,
    int vol,
    int sep)
{
    if (!sound_initialized)
        return;

    if (channel < 0 || channel >= NUM_CHANNELS)
        return;

    int left =
        ((254 - sep) * vol) / 127;

    int right =
        (sep * vol) / 127;

    if (left < 0)
        left = 0;
    else if (left > 255)
        left = 255;

    if (right < 0)
        right = 0;
    else if (right > 255)
        right = 255;

    SDL_LockAudioDevice(audio_device);

    channels[channel].left = left;
    channels[channel].right = right;

    SDL_UnlockAudioDevice(audio_device);
}


static int GC_StartSound(
    sfxinfo_t *sfx,
    int channel,
    int vol,
    int sep)
{
    byte *lump;
    unsigned int lumpLength;
    unsigned int sampleLength;
    unsigned int sampleRate;

    if (!sound_initialized)
        return -1;

    if (channel < 0 || channel >= NUM_CHANNELS)
        return -1;

    lump = W_CacheLumpNum(
        sfx->lumpnum,
        PU_STATIC
    );

    lumpLength =
        W_LumpLength(sfx->lumpnum);

    if (lumpLength < 8)
        return -1;

    /*
     * Doom DMX digital sound header:
     *
     * 00-01 : format (must be 0x0003)
     * 02-03 : sample rate
     * 04-07 : sample count
     */
    if (lump[0] != 0x03 ||
        lump[1] != 0x00)
    {
        return -1;
    }

    sampleRate =
        lump[2] |
        (lump[3] << 8);

    sampleLength =
        lump[4] |
        (lump[5] << 8) |
        (lump[6] << 16) |
        (lump[7] << 24);

    if (sampleLength > lumpLength - 8)
        return -1;

    if (sampleLength <= 48)
        return -1;

    /*
     * DMX historically ignores the first and last
     * 16 samples.
     */
    const uint8_t *samples =
        lump + 8 + 16;

    sampleLength -= 32;

    uint32_t step =
        (uint32_t)(
            ((uint64_t)sampleRate << 16)
            / audio_spec.freq
        );

    if (step == 0)
        step = 1;

    SDL_LockAudioDevice(audio_device);

    gc_channel_t *gc = &channels[channel];

    gc->active = false;

    gc->data = samples;
    gc->length = sampleLength;

    gc->position = 0;
    gc->step = step;

    gc->left =
        ((254 - sep) * vol) / 127;

    gc->right =
        (sep * vol) / 127;

    if (gc->left > 255)
        gc->left = 255;

    if (gc->right > 255)
        gc->right = 255;

    gc->active = true;

    SDL_UnlockAudioDevice(audio_device);

    return channel;
}


static void GC_StopSound(int channel)
{
    if (!sound_initialized)
        return;

    if (channel < 0 || channel >= NUM_CHANNELS)
        return;

    SDL_LockAudioDevice(audio_device);

    channels[channel].active = false;

    SDL_UnlockAudioDevice(audio_device);
}


static boolean GC_SoundIsPlaying(int channel)
{
    boolean playing;

    if (!sound_initialized)
        return false;

    if (channel < 0 || channel >= NUM_CHANNELS)
        return false;

    SDL_LockAudioDevice(audio_device);

    playing = channels[channel].active;

    SDL_UnlockAudioDevice(audio_device);

    return playing;
}


static void GC_CacheSounds(
    sfxinfo_t *sounds,
    int numSounds)
{
    (void)sounds;
    (void)numSounds;
}


static snddevice_t gc_sound_devices[] =
{
    SNDDEVICE_SB,
    SNDDEVICE_PAS,
    SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS,
    SNDDEVICE_AWE32
};


sound_module_t DG_sound_module =
{
    gc_sound_devices,
    sizeof(gc_sound_devices)
        / sizeof(gc_sound_devices[0]),

    GC_InitSound,
    GC_ShutdownSound,
    GC_GetSfxLumpNum,
    GC_UpdateSound,
    GC_UpdateSoundParams,
    GC_StartSound,
    GC_StopSound,
    GC_SoundIsPlaying,
    GC_CacheSounds
};


/*
 * Music is deliberately disabled for this milestone.
 *
 * FEATURE_SOUND causes DoomGeneric to expect DG_music_module
 * to exist at link time, even though -nomusic prevents it
 * from being initialized.
 */

static boolean GC_MusicInit(void)
{
    return false;
}

static void GC_MusicShutdown(void)
{
}

static void GC_SetMusicVolume(int volume)
{
    (void)volume;
}

static void GC_PauseMusic(void)
{
}

static void GC_ResumeMusic(void)
{
}

static void *GC_RegisterSong(void *data, int len)
{
    (void)data;
    (void)len;

    return NULL;
}

static void GC_UnRegisterSong(void *handle)
{
    (void)handle;
}

static void GC_PlaySong(void *handle, boolean looping)
{
    (void)handle;
    (void)looping;
}

static void GC_StopSong(void)
{
}

static boolean GC_MusicIsPlaying(void)
{
    return false;
}

static void GC_MusicPoll(void)
{
}


static snddevice_t gc_music_devices[] =
{
    SNDDEVICE_NONE
};


music_module_t DG_music_module =
{
    gc_music_devices,
    1,

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