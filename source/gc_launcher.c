/* ------------------------------------------------------------------------- */
/* DoomCube launcher                                                         */
/* ------------------------------------------------------------------------- */

#include "gc_debug.h"
#include "i_sound.h"

#include "gc_config.h"
#include "m_config.h"
#include "gc_launcher.h"
#include <SDL2/SDL_mixer.h>

#include <carryhandle/ch_controller_glyph_sdl.h>
#include <carryhandle/ch_memcard_ui.h>
#include <carryhandle/ch_splash.h>
#include "gc_memcard.h"
#include "gc_rumble.h"
#include "gc_regression.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <gccore.h>
#include <ogcsys.h>

#define GC_LAUNCHER_FONT_SCALE   3
#define GC_LAUNCHER_LINE_HEIGHT  32
#define GC_LAUNCHER_WIDTH        640
#define GC_LAUNCHER_DEADZONE     24


static void GC_LauncherProbeGlobalConfig(void)
{
    static gc_config_snapshot_t snapshot;

    static const char *const integerKeys[] =
    {
        "gc_turn_sensitivity",
        "gc_rumble_enabled",
        "gc_fire",
        "gc_use",
        "gc_run",
        "sfx_volume",
        "music_volume",
        "show_messages",
        "screenblocks",
        "detaillevel"
    };

    size_t i;

    if (!GC_ConfigSnapshotLoad(
            &snapshot))
    {
        DC_INFO(
            "DoomCube: launcher global config snapshot unavailable\n");

        return;
    }

    DC_INFO(
        "DoomCube: launcher global config snapshot loaded (%u bytes)\n",
        (unsigned int)snapshot.size);

    for (i = 0;
         i < sizeof(integerKeys) / sizeof(integerKeys[0]);
         ++i)
    {
        int value;

        if (GC_ConfigSnapshotFindInt(
                &snapshot,
                integerKeys[i],
                &value))
        {
            DC_INFO(
                "DoomCube: launcher config %s=%d\n",
                integerKeys[i],
                value);
        }
    }
}

#define GC_LAUNCHER_LOGO_PATH    "dvd:/launcher/doomcube.bmp"
#define GC_LAUNCHER_LOGO_Y       5

#define GC_DOOM_FONT_BITMAP_PATH  "dvd:/launcher/font/doomfont.bmp"
#define GC_DOOM_FONT_METRICS_PATH "dvd:/launcher/font/doomfont.txt"
#define GC_SPLASH_COLLAGE_PATH    "dvd:/launcher/splash.bmp"
#define GC_DOOM_MENU_LOGO_PATH    "dvd:/launcher/m_doom.bmp"
#define GC_DOOM_MENU_SKULL_PATH   "dvd:/launcher/m_skull1.bmp"
#define GC_SPLASH_CUBE_PATH       "dvd:/launcher/doomcube_splash.bmp"
#define GC_STUDIO_IDENT_PATH      "dvd:/launcher/sperge_brigade_studios.bmp"
#define GC_STUDIO_IDENT_AUDIO_PATH "dvd:/launcher/audio/cybsit.wav"
#define GC_CARRYHANDLE_IDENT_PATH "dvd:/launcher/carryhandle_powered_by.bmp"
#define GC_CARRYHANDLE_IDENT_FADE_MS 500u
#define GC_CARRYHANDLE_IDENT_HOLD_MS 1250u
#define GC_LAUNCHER_TIMIDITY_CFG          "dvd:/data/timidity/timidity.cfg"
#define GC_LAUNCHER_MUSIC_DOOM1_PATH      "dvd:/launcher/music/doom1.mid"
#define GC_LAUNCHER_MUSIC_DOOM_PATH       "dvd:/launcher/music/doom.mid"
#define GC_LAUNCHER_MUSIC_DOOM2_PATH      "dvd:/launcher/music/doom2.mid"
#define GC_LAUNCHER_MUSIC_TNT_PATH        "dvd:/launcher/music/tnt.mid"
#define GC_LAUNCHER_MUSIC_PLUTONIA_PATH   "dvd:/launcher/music/plutonia.mid"
#define GC_LAUNCHER_MUSIC_SIGIL_PATH      "dvd:/launcher/music/sigil.mid"
#define GC_LAUNCHER_MUSIC_SIGIL2_PATH     "dvd:/launcher/music/sigil2.mid"
#define GC_LAUNCHER_MUSIC_CUSTOM_PATH     "dvd:/launcher/music/custom.mid"
#define GC_LAUNCHER_MENU_CHOOSE_AUDIO_PATH "dvd:/launcher/audio/menu_choose.wav"
#define GC_STUDIO_IDENT_FADE_MS   750u
#define GC_STUDIO_IDENT_HOLD_MS   2000u
#define GC_STUDIO_IDENT_FRAME_MS  16u
#define GC_STUDIO_IDENT_HEIGHT    360
#define GC_MEMCARD_NORMAL_BACKGROUND_PATH \
    "dvd:/launcher/memcard/mwall4_1.bmp"
#define GC_MEMCARD_ERROR_BACKGROUND_PATH \
    "dvd:/launcher/memcard/pfub2.bmp"

#define GC_DOOM_FONT_MAX_CODE     127
#define GC_DOOM_FONT_EXPECTED_GLYPHS 64

#define GC_MAX_GAMES 8
#define GC_CUSTOM_GAME_INDEX 7
#define GC_OPTIONS_ENTRY_INDEX GC_MAX_GAMES
#define GC_LAUNCHER_ENTRY_COUNT (GC_MAX_GAMES + 1)
#define GC_OPTIONS_BACKGROUND_PATH "dvd:/launcher/options/interpic.bmp"

#define GC_PWAD_MANIFEST_PATH "dvd:/data/pwad/doomcube.lst"
#define GC_PWAD_DIRECTORY     "dvd:/data/pwad"

#define GC_MAX_PWADS          64
#define GC_PWAD_NAME_MAX      128
#define GC_PWAD_PATH_MAX      256

#define GC_CUSTOM_BASE_COUNT  4
typedef struct
{
    const char *name;
    const char *iwadPath;
    const char *pwadPath;
    const char *artPath;
    bool available;
    gc_savegame_id_t saveGameId;
} gc_game_entry_t;

static gc_game_entry_t gcGames[GC_MAX_GAMES] =
{
    {
        "DOOM SHAREWARE",
        "dvd:/data/wad/doom1.wad",
        NULL,
        "dvd:/launcher/titlepic/doom1.bmp",
        false,
        GC_SAVEGAME_DOOM1
    },
    {
        "DOOM",
        "dvd:/data/wad/doom.wad",
        NULL,
        "dvd:/launcher/titlepic/doom.bmp",
        false,
        GC_SAVEGAME_DOOM
    },
    {
        "DOOM II",
        "dvd:/data/wad/doom2.wad",
        NULL,
        "dvd:/launcher/titlepic/doom2.bmp",
        false,
        GC_SAVEGAME_DOOM2
    },
    {
        "TNT: EVILUTION",
        "dvd:/data/wad/tnt.wad",
        NULL,
        "dvd:/launcher/titlepic/tnt.bmp",
        false,
        GC_SAVEGAME_TNT
    },
    {
        "PLUTONIA",
        "dvd:/data/wad/plutonia.wad",
        NULL,
        "dvd:/launcher/titlepic/plutonia.bmp",
        false,
        GC_SAVEGAME_PLUTONIA
    },
    {
        "SIGIL",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/doom/SIGIL_V1_23.wad",
        "dvd:/launcher/titlepic/sigil.bmp",
        false,
        GC_SAVEGAME_DOOM
    },
    {
        "SIGIL II",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/doom/SIGIL_II_V1_0.WAD",
        "dvd:/launcher/titlepic/sigil2.bmp",
        false,
        GC_SAVEGAME_DOOM
    },
    {
        "CUSTOM",
        NULL,
        NULL,
        NULL,
        false,
        GC_SAVEGAME_DOOM
    }
};


typedef struct
{
    const char *name;
    const char *directory;
    const char *iwadPath;
    gc_savegame_id_t saveGameId;
} gc_custom_base_t;

static const gc_custom_base_t gcCustomBases[GC_CUSTOM_BASE_COUNT] =
{
    {
        "DOOM",
        "doom",
        "dvd:/data/wad/doom.wad",
        GC_SAVEGAME_DOOM
    },
    {
        "DOOM II",
        "doom2",
        "dvd:/data/wad/doom2.wad",
        GC_SAVEGAME_DOOM2
    },
    {
        "TNT: EVILUTION",
        "tnt",
        "dvd:/data/wad/tnt.wad",
        GC_SAVEGAME_TNT
    },
    {
        "PLUTONIA",
        "plutonia",
        "dvd:/data/wad/plutonia.wad",
        GC_SAVEGAME_PLUTONIA
    }
};

typedef struct
{
    char name[GC_PWAD_NAME_MAX];
    char path[GC_PWAD_PATH_MAX];
    int baseIndex;
} gc_pwad_entry_t;

static gc_pwad_entry_t gcPwads[GC_MAX_PWADS];

static int GC_PwadBaseIndex(const char *manifestPath)
{
    int i;

    if (manifestPath == NULL)
        return -1;

    for (i = 0; i < GC_CUSTOM_BASE_COUNT; ++i)
    {
        size_t prefixLength =
            strlen(gcCustomBases[i].directory);

        if (strncmp(
                manifestPath,
                gcCustomBases[i].directory,
                prefixLength) == 0 &&
            manifestPath[prefixLength] == '/')
        {
            return i;
        }
    }

    return -1;
}

static int gcAvailableGameCount;
static int gcAvailablePwadCount;

static bool GC_FileExists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0;
}

static bool GC_CustomBaseAvailable(int baseIndex)
{
    int i;

    if (baseIndex < 0 ||
        baseIndex >= GC_CUSTOM_BASE_COUNT)
    {
        return false;
    }

    if (!GC_FileExists(
            gcCustomBases[baseIndex].iwadPath))
    {
        return false;
    }

    for (i = 0; i < gcAvailablePwadCount; ++i)
    {
        if (gcPwads[i].baseIndex == baseIndex)
        {
            return true;
        }
    }

    return false;
}

static bool GC_CustomAnyBaseAvailable(void)
{
    int i;

    for (i = 0; i < GC_CUSTOM_BASE_COUNT; ++i)
    {
        if (GC_CustomBaseAvailable(i))
        {
            return true;
        }
    }

    return false;
}

static int GC_FirstAvailableCustomBase(void)
{
    int i;

    for (i = 0; i < GC_CUSTOM_BASE_COUNT; ++i)
    {
        if (GC_CustomBaseAvailable(i))
        {
            return i;
        }
    }

    return -1;
}

static int GC_NextAvailableCustomBase(
    int current,
    int direction)
{
    int attempts;

    for (attempts = 0;
         attempts < GC_CUSTOM_BASE_COUNT;
         ++attempts)
    {
        current += direction;

        if (current < 0)
            current = GC_CUSTOM_BASE_COUNT - 1;
        else if (current >= GC_CUSTOM_BASE_COUNT)
            current = 0;

        if (GC_CustomBaseAvailable(current))
        {
            return current;
        }
    }

    return -1;
}


static int GC_CustomPwadCount(int baseIndex)
{
    int i;
    int count = 0;

    for (i = 0; i < gcAvailablePwadCount; ++i)
    {
        if (gcPwads[i].baseIndex == baseIndex)
        {
            ++count;
        }
    }

    return count;
}


static int GC_FirstCustomPwad(int baseIndex)
{
    int i;

    for (i = 0; i < gcAvailablePwadCount; ++i)
    {
        if (gcPwads[i].baseIndex == baseIndex)
        {
            return i;
        }
    }

    return -1;
}


static int GC_NextCustomPwad(
    int current,
    int baseIndex,
    int direction)
{
    int attempts;

    if (gcAvailablePwadCount <= 0)
    {
        return -1;
    }

    for (attempts = 0;
         attempts < gcAvailablePwadCount;
         ++attempts)
    {
        current += direction;

        if (current < 0)
        {
            current =
                gcAvailablePwadCount - 1;
        }
        else if (current >= gcAvailablePwadCount)
        {
            current = 0;
        }

        if (gcPwads[current].baseIndex == baseIndex)
        {
            return current;
        }
    }

    return -1;
}


static int GC_CustomPwadOrdinal(
    int baseIndex,
    int pwadIndex)
{
    int i;
    int ordinal = 0;

    for (i = 0; i < gcAvailablePwadCount; ++i)
    {
        if (gcPwads[i].baseIndex != baseIndex)
        {
            continue;
        }

        if (i == pwadIndex)
        {
            return ordinal;
        }

        ++ordinal;
    }

    return -1;
}


static int GC_LauncherScanPwads(void)
{
    FILE *manifest;
    char line[GC_PWAD_PATH_MAX];

    gcAvailablePwadCount = 0;

    DC_DEBUG("DoomCube: ---- AVAILABLE PWADS ----\n");

    manifest =
        fopen(
            GC_PWAD_MANIFEST_PATH,
            "rb");

    if (manifest == NULL)
    {
        DC_DEBUG(
            "DoomCube: no PWAD manifest found\n");

        return 0;
    }

    while (fgets(line, sizeof(line), manifest) != NULL)
    {
        size_t length;
        int written;

        /*
         * The generated manifest contains one filename per line.
         * Strip either Unix or Windows-style line endings.
         */
        length = strcspn(line, "\r\n");

        /*
         * If the buffer filled without reaching a line ending, the
         * manifest entry is too long for DoomCube's launcher.
         */
        if (line[length] == '\0' &&
            !feof(manifest) &&
            length == sizeof(line) - 1)
        {
            int c;

            while ((c = fgetc(manifest)) != '\n' &&
                   c != EOF)
            {
                /* Drain the rest of the oversized line. */
            }

            DC_WARN(
                "DoomCube: PWAD manifest entry too long; skipping\n");

            continue;
        }

        line[length] = '\0';

        if (line[0] == '\0')
        {
            continue;
        }

        if (gcAvailablePwadCount >= GC_MAX_PWADS)
        {
            DC_WARN(
                "DoomCube: PWAD limit reached (%d)\n",
                GC_MAX_PWADS);

            break;
        }

        written =
            snprintf(
                gcPwads[gcAvailablePwadCount].path,
                sizeof(gcPwads[gcAvailablePwadCount].path),
                "%s/%s",
                GC_PWAD_DIRECTORY,
                line);

        if (written < 0 ||
            (size_t)written >=
                sizeof(gcPwads[gcAvailablePwadCount].path))
        {
            DC_WARN(
                "DoomCube: PWAD path too long: %s\n",
                line);

            continue;
        }

        if (!GC_FileExists(
                gcPwads[gcAvailablePwadCount].path))
        {
            DC_WARN(
                "DoomCube: PWAD listed but missing: %s\n",
                line);

            continue;
        }

        {
            const char *displayName =
                strrchr(line, '/');

            if (displayName != NULL)
                ++displayName;
            else
                displayName = line;

            snprintf(
                gcPwads[gcAvailablePwadCount].name,
                sizeof(gcPwads[gcAvailablePwadCount].name),
                "%.*s",
                (int)sizeof(
                    gcPwads[gcAvailablePwadCount].name) - 1,
                displayName);
        }

        gcPwads[gcAvailablePwadCount].baseIndex =
            GC_PwadBaseIndex(line);

        if (gcPwads[gcAvailablePwadCount].baseIndex >= 0)
        {
            DC_DEBUG(
                "DoomCube: found PWAD %s (%s) [base %s]\n",
                gcPwads[gcAvailablePwadCount].name,
                gcPwads[gcAvailablePwadCount].path,
                gcCustomBases[
                    gcPwads[gcAvailablePwadCount].baseIndex
                ].name);
        }
        else
        {
            /*
             * Keep unbucketed entries available for automated test and
             * regression lookup while the old scanner still exists.
             * CUSTOM will only expose bucketed entries.
             */
            DC_DEBUG(
                "DoomCube: found unbucketed PWAD %s (%s)\n",
                gcPwads[gcAvailablePwadCount].name,
                gcPwads[gcAvailablePwadCount].path);
        }

        ++gcAvailablePwadCount;
    }

    fclose(manifest);

    DC_DEBUG(
        "DoomCube: %d PWAD(s) available\n",
        gcAvailablePwadCount);

    return gcAvailablePwadCount;
}

static SDL_Texture *GC_LoadLauncherBitmap(
    SDL_Renderer *renderer,
    const char *path,
    const char *description)
{
    SDL_Surface *loaded;
    SDL_Surface *converted;
    SDL_Texture *texture;

    if (path == NULL)
    {
        return NULL;
    }

    loaded = SDL_LoadBMP(path);

    if (loaded == NULL)
    {
        DC_WARN(
            "DoomCube: launcher %s load failed from %s: %s\n",
            description,
            path,
            SDL_GetError());

        return NULL;
    }

    DC_DEBUG(
        "DoomCube: launcher %s BMP: %dx%d, "
        "format=%s, pitch=%d\n",
        description,
        loaded->w,
        loaded->h,
        SDL_GetPixelFormatName(
            loaded->format->format),
        loaded->pitch);

    /*
     * Do not hand SDL_CreateTextureFromSurface() whatever native
     * pixel format SDL_LoadBMP() happened to produce.
     *
     * Convert explicitly to 32-bit RGBA first.  This is the same
     * GameCube-safe path already proven by the DoomCube splash logo.
     */
    converted = SDL_ConvertSurfaceFormat(
        loaded,
        SDL_PIXELFORMAT_RGBA32,
        0);

    SDL_FreeSurface(loaded);

    if (converted == NULL)
    {
        DC_WARN(
            "DoomCube: launcher %s conversion failed "
            "for %s: %s\n",
            description,
            path,
            SDL_GetError());

        return NULL;
    }

    DC_DEBUG(
        "DoomCube: converted launcher %s: %dx%d, "
        "format=%s, pitch=%d\n",
        description,
        converted->w,
        converted->h,
        SDL_GetPixelFormatName(
            converted->format->format),
        converted->pitch);

    texture =
        SDL_CreateTextureFromSurface(
            renderer,
            converted);

    SDL_FreeSurface(converted);

    if (texture == NULL)
    {
        DC_WARN(
            "DoomCube: launcher %s texture creation "
            "failed for %s: %s\n",
            description,
            path,
            SDL_GetError());

        return NULL;
    }

    DC_DEBUG(
        "DoomCube: launcher %s loaded from %s\n",
        description,
        path);

    return texture;
}


static SDL_Texture *GC_LoadLauncherKeyedBitmap(
    SDL_Renderer *renderer,
    const char *path,
    const char *description,
    Uint8 keyRed,
    Uint8 keyGreen,
    Uint8 keyBlue)
{
    SDL_Surface *loaded;
    SDL_Surface *converted;
    SDL_Texture *texture;
    Uint32 colorKey;

    if (renderer == NULL ||
        path == NULL ||
        path[0] == '\0')
    {
        return NULL;
    }

    loaded =
        SDL_LoadBMP(path);

    if (loaded == NULL)
    {
        DC_WARN(
            "DoomCube: failed to load %s %s: %s\n",
            description,
            path,
            SDL_GetError());

        return NULL;
    }

    converted =
        SDL_ConvertSurfaceFormat(
            loaded,
            SDL_PIXELFORMAT_RGBA32,
            0);

    SDL_FreeSurface(loaded);

    if (converted == NULL)
    {
        DC_WARN(
            "DoomCube: failed to convert %s %s: %s\n",
            description,
            path,
            SDL_GetError());

        return NULL;
    }

    colorKey =
        SDL_MapRGB(
            converted->format,
            keyRed,
            keyGreen,
            keyBlue);

    if (SDL_SetColorKey(
            converted,
            SDL_TRUE,
            colorKey) != 0)
    {
        DC_WARN(
            "DoomCube: failed to color-key %s %s: %s\n",
            description,
            path,
            SDL_GetError());

        SDL_FreeSurface(converted);
        return NULL;
    }

    texture =
        SDL_CreateTextureFromSurface(
            renderer,
            converted);

    SDL_FreeSurface(converted);

    if (texture == NULL)
    {
        DC_WARN(
            "DoomCube: failed to texture %s %s: %s\n",
            description,
            path,
            SDL_GetError());

        return NULL;
    }

    return texture;
}

#define GC_DOOM_MENU_SKULL_GAP       8
#define GC_DOOM_MENU_SKULL_Y_OFFSET -6

static SDL_Texture *gcDoomMenuSkullTexture = NULL;
static int gcDoomMenuSkullLoadAttempted = 0;


static SDL_Texture *GC_LoadDoomMenuSkull(
    SDL_Renderer *renderer)
{
    if (!gcDoomMenuSkullLoadAttempted)
    {
        gcDoomMenuSkullLoadAttempted = 1;

        gcDoomMenuSkullTexture =
            GC_LoadLauncherKeyedBitmap(
                renderer,
                GC_DOOM_MENU_SKULL_PATH,
                "M_SKULL1",
                255,
                0,
                255);
    }

    return gcDoomMenuSkullTexture;
}


static int GC_DrawDoomMenuSkullCursor(
    SDL_Renderer *renderer,
    int textX,
    int textY)
{
    SDL_Texture *texture;
    SDL_Rect dst;
    int width;
    int height;

    texture =
        GC_LoadDoomMenuSkull(
            renderer);

    if (texture == NULL)
        return 0;

    if (SDL_QueryTexture(
            texture,
            NULL,
            NULL,
            &width,
            &height) != 0)
    {
        DC_WARN(
            "DoomCube: M_SKULL1 texture query failed: %s\n",
            SDL_GetError());

        return 0;
    }

    dst.x =
        textX
        - GC_DOOM_MENU_SKULL_GAP
        - width;

    dst.y =
        textY
        + GC_DOOM_MENU_SKULL_Y_OFFSET;

    dst.w = width;
    dst.h = height;

    SDL_SetTextureColorMod(
        texture,
        255,
        255,
        255);

    SDL_SetTextureAlphaMod(
        texture,
        255);

    if (SDL_RenderCopy(
            renderer,
            texture,
            NULL,
            &dst) != 0)
    {
        DC_WARN(
            "DoomCube: M_SKULL1 render failed: %s\n",
            SDL_GetError());

        return 0;
    }

    return 1;
}


static void GC_DrawCustomSelectionFallback(
    SDL_Renderer *renderer,
    int y)
{
    SDL_Rect marker =
    {
        55,
        y - 5,
        530,
        28
    };

    SDL_SetRenderDrawColor(
        renderer,
        70,
        45,
        120,
        255);

    SDL_RenderFillRect(
        renderer,
        &marker);

    SDL_SetRenderDrawColor(
        renderer,
        255,
        255,
        255,
        255);
}


static void GC_DoomMenuSkullShutdown(void)
{
    if (gcDoomMenuSkullTexture != NULL)
    {
        SDL_DestroyTexture(
            gcDoomMenuSkullTexture);

        gcDoomMenuSkullTexture = NULL;
    }

    gcDoomMenuSkullLoadAttempted = 0;
}



static SDL_Texture *GC_LoadLauncherLogo(
    SDL_Renderer *renderer)
{
    return GC_LoadLauncherBitmap(
        renderer,
        GC_LAUNCHER_LOGO_PATH,
        "logo");
}


static int GC_LauncherScanGames(void)
{
    int i;

    gcAvailableGameCount = 0;

    DC_DEBUG("DoomCube: ---- AVAILABLE GAMES ----\n");

    for (i = 0; i < GC_MAX_GAMES; ++i)
    {
        if (i == GC_CUSTOM_GAME_INDEX)
        {
            gcGames[i].available =
                GC_CustomAnyBaseAvailable();

            if (!gcGames[i].available)
                continue;

            ++gcAvailableGameCount;

            DC_DEBUG(
                "DoomCube: found CUSTOM launcher entry\n");

            continue;
        }

        gcGames[i].available =
            GC_FileExists(gcGames[i].iwadPath);

        if (gcGames[i].available &&
            gcGames[i].pwadPath != NULL)
        {
            gcGames[i].available =
                GC_FileExists(gcGames[i].pwadPath);
        }

        if (!gcGames[i].available)
            continue;

        ++gcAvailableGameCount;

        DC_DEBUG(
            "DoomCube: found %s (%s%s%s)\n",
            gcGames[i].name,
            gcGames[i].iwadPath,
            gcGames[i].pwadPath != NULL ? " + " : "",
            gcGames[i].pwadPath != NULL ? gcGames[i].pwadPath : "");
    }

    DC_DEBUG(
        "DoomCube: %d game(s) available\n",
        gcAvailableGameCount);

    return gcAvailableGameCount;
}

typedef struct
{
    bool valid;
    int x;
    int y;
    int width;
    int height;
    int leftOffset;
    int topOffset;
    int advance;
} gc_doom_font_glyph_t;


static gc_doom_font_glyph_t
    gcDoomFontGlyphs[GC_DOOM_FONT_MAX_CODE + 1];

static bool gcDoomFontMetricsLoaded;
static int gcDoomFontSpaceAdvance = 4;
static int gcDoomFontLineHeight = 9;
static SDL_Texture *gcDoomFontTexture;
static bool gcDoomFontLoadWarningShown;


/*
 * Emergency built-in font.
 *
 * The normal DoomCube UI should never use this when generated launcher
 * assets are present.  Keep it so missing/corrupt font assets do not
 * turn an error screen into a completely blank screen.
 */
static const uint8_t *GC_FallbackFontGlyph(char c)
{
    static const uint8_t blank[7] =
    {
        0, 0, 0, 0, 0, 0, 0
    };

    static const uint8_t glyphs[26][7] =
    {
        {14,17,17,31,17,17,17}, /* A */
        {30,17,17,30,17,17,30}, /* B */
        {14,17,16,16,16,17,14}, /* C */
        {30,17,17,17,17,17,30}, /* D */
        {31,16,16,30,16,16,31}, /* E */
        {31,16,16,30,16,16,16}, /* F */
        {14,17,16,23,17,17,15}, /* G */
        {17,17,17,31,17,17,17}, /* H */
        {14,4,4,4,4,4,14},      /* I */
        {7,2,2,2,18,18,12},     /* J */
        {17,18,20,24,20,18,17}, /* K */
        {16,16,16,16,16,16,31}, /* L */
        {17,27,21,21,17,17,17}, /* M */
        {17,25,21,19,17,17,17}, /* N */
        {14,17,17,17,17,17,14}, /* O */
        {30,17,17,30,16,16,16}, /* P */
        {14,17,17,17,21,18,13}, /* Q */
        {30,17,17,30,20,18,17}, /* R */
        {15,16,16,14,1,1,30},   /* S */
        {31,4,4,4,4,4,4},       /* T */
        {17,17,17,17,17,17,14}, /* U */
        {17,17,17,17,17,10,4},  /* V */
        {17,17,17,21,21,21,10}, /* W */
        {17,17,10,4,10,17,17},  /* X */
        {17,17,10,4,4,4,4},     /* Y */
        {31,1,2,4,8,16,31}      /* Z */
    };

    static const uint8_t colon[7] =
        { 0, 4, 4, 0, 4, 4, 0 };

    static const uint8_t dash[7] =
        { 0, 0, 0, 31, 0, 0, 0 };

    static const uint8_t period[7] =
        { 0, 0, 0, 0, 0, 4, 4 };

    static const uint8_t lparen[7] =
        { 2, 4, 8, 8, 8, 4, 2 };

    static const uint8_t rparen[7] =
        { 8, 4, 2, 2, 2, 4, 8 };

    static const uint8_t digits[10][7] =
    {
        {14,17,19,21,25,17,14},
        {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2},
        {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},
        {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14}
    };

    static const uint8_t copyleft[7] =
    {
        14,
        17,
        13,
        9,
        13,
        17,
        14
    };

    unsigned char uc =
        (unsigned char)toupper(
            (unsigned char)c);

    if (uc >= 'A' && uc <= 'Z')
        return glyphs[uc - 'A'];

    if (uc >= '0' && uc <= '9')
        return digits[uc - '0'];

    switch (uc)
    {
        case ':': return colon;
        case '-': return dash;
        case '.': return period;
        case '(': return lparen;
        case ')': return rparen;
        case '@': return copyleft;
        default:  return blank;
    }
}


static void GC_DrawFallbackChar(
    SDL_Renderer *renderer,
    int x,
    int y,
    char c,
    int scale)
{
    const uint8_t *glyph =
        GC_FallbackFontGlyph(c);

    SDL_Rect pixel =
    {
        0,
        0,
        scale,
        scale
    };

    int row;
    int col;

    for (row = 0; row < 7; ++row)
    {
        for (col = 0; col < 5; ++col)
        {
            if ((glyph[row] &
                 (1u << (4 - col))) == 0)
            {
                continue;
            }

            pixel.x =
                x + col * scale;

            pixel.y =
                y + row * scale;

            SDL_RenderFillRect(
                renderer,
                &pixel);
        }
    }
}


static void GC_DrawFallbackText(
    SDL_Renderer *renderer,
    int x,
    int y,
    const char *text,
    int scale)
{
    size_t i;
    size_t length;

    if (text == NULL)
        return;

    length = strlen(text);

    for (i = 0; i < length; ++i)
    {
        GC_DrawFallbackChar(
            renderer,
            x + (int)i * 6 * scale,
            y,
            text[i],
            scale);
    }
}


static int GC_FallbackTextWidth(
    const char *text,
    int scale)
{
    if (text == NULL)
        return 0;

    return
        (int)strlen(text)
        * 6
        * scale;
}


static bool GC_LoadDoomFontMetrics(void)
{
    FILE *file;
    char line[256];
    gc_doom_font_glyph_t
        parsed[GC_DOOM_FONT_MAX_CODE + 1];

    int parsedSpace = 4;
    int parsedLineHeight = 9;
    int glyphCount = 0;

    if (gcDoomFontMetricsLoaded)
        return true;

    memset(
        parsed,
        0,
        sizeof(parsed));

    file =
        fopen(
            GC_DOOM_FONT_METRICS_PATH,
            "r");

    if (file == NULL)
    {
        return false;
    }

    if (fgets(
            line,
            sizeof(line),
            file) == NULL ||
        strcmp(
            line,
            "DOOMCUBE_FONT_V1\n") != 0)
    {
        fclose(file);
        return false;
    }

    while (fgets(
            line,
            sizeof(line),
            file) != NULL)
    {
        int code;
        int x;
        int y;
        int width;
        int height;
        int leftOffset;
        int topOffset;
        int advance;
        int value;

        if (sscanf(
                line,
                "space %d",
                &value) == 1)
        {
            if (value <= 0 ||
                value > 32)
            {
                fclose(file);
                return false;
            }

            parsedSpace = value;
            continue;
        }

        if (sscanf(
                line,
                "line_height %d",
                &value) == 1)
        {
            if (value <= 0 ||
                value > 32)
            {
                fclose(file);
                return false;
            }

            parsedLineHeight = value;
            continue;
        }

        if (sscanf(
                line,
                "glyph %d %d %d %d %d %d %d %d",
                &code,
                &x,
                &y,
                &width,
                &height,
                &leftOffset,
                &topOffset,
                &advance) == 8)
        {
            if (code < 0 ||
                code > GC_DOOM_FONT_MAX_CODE ||
                width <= 0 ||
                width > 32 ||
                height <= 0 ||
                height > 32 ||
                advance <= 0 ||
                advance > 32)
            {
                fclose(file);
                return false;
            }

            parsed[code].valid = true;
            parsed[code].x = x;
            parsed[code].y = y;
            parsed[code].width = width;
            parsed[code].height = height;
            parsed[code].leftOffset = leftOffset;
            parsed[code].topOffset = topOffset;
            parsed[code].advance = advance;

            ++glyphCount;
        }
    }

    fclose(file);

    if (glyphCount != GC_DOOM_FONT_EXPECTED_GLYPHS ||
        !parsed['?'].valid ||
        !parsed['A'].valid ||
        !parsed['Z'].valid ||
        !parsed['0'].valid ||
        !parsed['9'].valid)
    {
        return false;
    }

    memcpy(
        gcDoomFontGlyphs,
        parsed,
        sizeof(parsed));

    gcDoomFontSpaceAdvance =
        parsedSpace;

    gcDoomFontLineHeight =
        parsedLineHeight;

    gcDoomFontMetricsLoaded =
        true;

    DC_DEBUG(
        "DoomCube: Doom launcher font metrics loaded: "
        "%d glyphs, line=%d, space=%d\n",
        glyphCount,
        gcDoomFontLineHeight,
        gcDoomFontSpaceAdvance);

    return true;
}


static bool GC_LoadDoomFontTexture(
    SDL_Renderer *renderer)
{
    SDL_Surface *loaded;
    SDL_Surface *converted;
    SDL_Texture *texture;
    Uint32 colorKey;

    if (gcDoomFontTexture != NULL)
        return true;

    if (renderer == NULL ||
        !GC_LoadDoomFontMetrics())
    {
        return false;
    }

    loaded =
        SDL_LoadBMP(
            GC_DOOM_FONT_BITMAP_PATH);

    if (loaded == NULL)
        goto failed;

    converted =
        SDL_ConvertSurfaceFormat(
            loaded,
            SDL_PIXELFORMAT_RGBA32,
            0);

    SDL_FreeSurface(loaded);

    if (converted == NULL)
        goto failed;

    /*
     * Step 8's pack-time font generator reserves pure magenta for
     * transparency while preserving the original Doom PLAYPAL colours
     * of every visible STCFN pixel.
     */
    colorKey =
        SDL_MapRGB(
            converted->format,
            255,
            0,
            255);

    if (SDL_SetColorKey(
            converted,
            SDL_TRUE,
            colorKey) != 0)
    {
        SDL_FreeSurface(converted);
        goto failed;
    }

    texture =
        SDL_CreateTextureFromSurface(
            renderer,
            converted);

    SDL_FreeSurface(converted);

    if (texture == NULL)
        goto failed;

    if (SDL_SetTextureBlendMode(
            texture,
            SDL_BLENDMODE_BLEND) != 0)
    {
        SDL_DestroyTexture(texture);
        goto failed;
    }

    gcDoomFontTexture =
        texture;

    gcDoomFontLoadWarningShown =
        false;

    DC_DEBUG(
        "DoomCube: Doom launcher font loaded from %s\n",
        GC_DOOM_FONT_BITMAP_PATH);

    return true;

failed:
    if (!gcDoomFontLoadWarningShown)
    {
        DC_WARN(
            "DoomCube: Doom launcher font unavailable: %s; "
            "using emergency built-in font\n",
            SDL_GetError());

        gcDoomFontLoadWarningShown =
            true;
    }

    return false;
}


static void GC_DoomFontShutdown(void)
{
    if (gcDoomFontTexture != NULL)
    {
        SDL_DestroyTexture(
            gcDoomFontTexture);

        gcDoomFontTexture = NULL;
    }
}


static int GC_DoomFontCode(char c)
{
    unsigned char uc =
        (unsigned char)c;

    if (uc == ' ')
        return ' ';

    uc =
        (unsigned char)toupper(uc);

    if (uc <= GC_DOOM_FONT_MAX_CODE &&
        gcDoomFontGlyphs[uc].valid)
    {
        return (int)uc;
    }

    return '?';
}


static int GC_DoomFontPhysicalScale(
    int logicalScale)
{
    if (logicalScale <= 0)
        return 1;

    if (logicalScale <= 2)
        return 1;

    if (logicalScale <= 4)
        return 2;

    return 4;
}


static int GC_DoomTextWidth(
    const char *text,
    int scale)
{
    int width = 0;
    int renderScale;
    size_t i;

    if (text == NULL ||
        scale <= 0 ||
        !GC_LoadDoomFontMetrics())
    {
        return 0;
    }

    renderScale =
        GC_DoomFontPhysicalScale(
            scale);

    for (i = 0; text[i] != '\0'; ++i)
    {
        int code =
            GC_DoomFontCode(
                text[i]);

        if (code == ' ')
        {
            width +=
                gcDoomFontSpaceAdvance
                * renderScale;
        }
        else
        {
            width +=
                gcDoomFontGlyphs[
                    code
                ].advance
                * renderScale;
        }
    }

    /*
     * The stored advance contains Doom's one-pixel inter-glyph gap.
     * Do not count that final gap when centering a complete string.
     */
    if (width > 0)
        width -= renderScale;

    return width;
}


static void GC_DrawDoomText(
    SDL_Renderer *renderer,
    int x,
    int y,
    const char *text,
    int scale)
{
    int penX = x;
    int renderScale;
    Uint8 textAlpha = 255;
    Uint8 ignoredRed;
    Uint8 ignoredGreen;
    Uint8 ignoredBlue;
    size_t i;

    if (text == NULL ||
        scale <= 0 ||
        gcDoomFontTexture == NULL)
    {
        return;
    }

    renderScale =
        GC_DoomFontPhysicalScale(
            scale);

    /*
     * Render the atlas with neutral modulation so its original Doom
     * red/orange PLAYPAL shading reaches the screen unchanged.
     */
    SDL_SetTextureColorMod(
        gcDoomFontTexture,
        255,
        255,
        255);

    if (SDL_GetRenderDrawColor(
            renderer,
            &ignoredRed,
            &ignoredGreen,
            &ignoredBlue,
            &textAlpha) != 0)
    {
        textAlpha = 255;
    }

    SDL_SetTextureAlphaMod(
        gcDoomFontTexture,
        textAlpha);

    for (i = 0; text[i] != '\0'; ++i)
    {
        int code =
            GC_DoomFontCode(
                text[i]);

        gc_doom_font_glyph_t *glyph;

        if (code == ' ')
        {
            penX +=
                gcDoomFontSpaceAdvance
                * renderScale;

            continue;
        }

        glyph =
            &gcDoomFontGlyphs[code];

        {
            SDL_Rect source =
            {
                glyph->x,
                glyph->y,
                glyph->width,
                glyph->height
            };

            SDL_Rect destination =
            {
                penX -
                    glyph->leftOffset
                    * renderScale,

                y -
                    glyph->topOffset
                    * renderScale,

                glyph->width
                    * renderScale,

                glyph->height
                    * renderScale
            };

            SDL_RenderCopy(
                renderer,
                gcDoomFontTexture,
                &source,
                &destination);
        }

        penX +=
            glyph->advance
            * renderScale;
    }
}


static void GC_DrawText(
    SDL_Renderer *renderer,
    int x,
    int y,
    const char *text,
    int scale)
{
    if (GC_LoadDoomFontTexture(renderer))
    {
        GC_DrawDoomText(
            renderer,
            x,
            y,
            text,
            scale);

        return;
    }

    GC_DrawFallbackText(
        renderer,
        x,
        y,
        text,
        scale);
}


static int GC_TextWidth(
    const char *text,
    int scale)
{
    int doomWidth;

    doomWidth =
        GC_DoomTextWidth(
            text,
            scale);

    if (doomWidth > 0 ||
        (text != NULL &&
         text[0] == '\0'))
    {
        return doomWidth;
    }

    return GC_FallbackTextWidth(
        text,
        scale);
}


static void GC_HideRedundantShareware(void)
{
    /*
     * Campaign table order is:
     *   0 = DOOM Shareware
     *   1 = full DOOM
     *
     * Shareware remains a valid standalone campaign when it is the
     * only DOOM IWAD.  When full DOOM is present, showing both is
     * redundant and makes the carousel/splash noisier.
     */
    if (gcGames[0].available &&
        gcGames[1].available)
    {
        gcGames[0].available = false;

        if (gcAvailableGameCount > 0)
            --gcAvailableGameCount;

        DC_DEBUG(
            "DoomCube: hiding DOOM Shareware because full DOOM is available\n");
    }
}


#define GC_CAROUSEL_NAV_GLYPH_SIZE       40
#define GC_CAROUSEL_NAV_GLYPH_GAP        10
#define GC_CAROUSEL_NAV_GLYPH_Y         360

#define GC_CAROUSEL_ACTION_GLYPH_SIZE    36
#define GC_CAROUSEL_ACTION_GLYPH_GAP      8
#define GC_CAROUSEL_ACTION_PAIR_GAP      28
#define GC_CAROUSEL_ACTION_GLYPH_Y      435
#define GC_CAROUSEL_ACTION_TEXT_Y       448

static SDL_Texture *gcCarouselDpadLeftGlyph = NULL;
static SDL_Texture *gcCarouselDpadRightGlyph = NULL;
static SDL_Texture *gcCarouselAGlyph = NULL;
static SDL_Texture *gcCarouselBGlyph = NULL;

static SDL_Texture *GC_LoadCarouselControllerGlyph(
    SDL_Renderer *renderer,
    SDL_Texture **cache,
    CH_ControllerGlyph glyph,
    int rasterSize,
    const char *label)
{
    if (*cache == NULL)
    {
        *cache =
            CH_ControllerGlyphSDLLoadTexture(
                renderer,
                "dvd:/",
                glyph,
                rasterSize);

        if (*cache == NULL)
        {
            DC_WARN(
                "DoomCube: launcher %s controller glyph load failed: %s\n",
                label,
                SDL_GetError());
        }
    }

    return *cache;
}

static void GC_DrawCarouselControllerGlyph(
    SDL_Renderer *renderer,
    SDL_Texture *texture,
    int x,
    int y,
    int size)
{
    SDL_Rect dst = { x, y, size, size };

    if (texture == NULL)
        return;

    SDL_SetTextureColorMod(texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture, 255);
    SDL_RenderCopy(renderer, texture, NULL, &dst);
}


static bool GC_LauncherEntryAvailable(int index)
{
    if (index == GC_OPTIONS_ENTRY_INDEX) return true;
    return index >= 0 && index < GC_MAX_GAMES && gcGames[index].available;
}

static const char *GC_LauncherEntryName(int index)
{
    if (index == GC_OPTIONS_ENTRY_INDEX) return "OPTIONS";
    if (index < 0 || index >= GC_MAX_GAMES) return "";
    return gcGames[index].name;
}

static const char *GC_LauncherEntryArtPath(int index)
{
    if (index == GC_OPTIONS_ENTRY_INDEX) return GC_OPTIONS_BACKGROUND_PATH;
    if (index < 0 || index >= GC_MAX_GAMES) return NULL;
    return gcGames[index].artPath;
}

static int GC_FirstAvailableEntry(void)
{
    int i;
    for (i = 0; i < GC_LAUNCHER_ENTRY_COUNT; ++i)
        if (GC_LauncherEntryAvailable(i)) return i;
    return -1;
}

static int GC_NextAvailableEntry(int current, int direction)
{
    int candidate=current;
    int attempts;
    if (direction == 0) return current;
    for (attempts=0; attempts<GC_LAUNCHER_ENTRY_COUNT; ++attempts)
    {
        candidate += direction < 0 ? -1 : 1;
        if (candidate < 0) candidate=GC_LAUNCHER_ENTRY_COUNT-1;
        else if (candidate >= GC_LAUNCHER_ENTRY_COUNT) candidate=0;
        if (GC_LauncherEntryAvailable(candidate)) return candidate;
    }
    return -1;
}

static void GC_DrawLauncher(
    SDL_Renderer *renderer,
    int selected)
{
    SDL_Texture *background = NULL;
    bool titlepicLoaded = false;
    int previous;
    int next;
    int textWidth;
    SDL_Texture *dpadLeftGlyph;
    SDL_Texture *dpadRightGlyph;
    SDL_Texture *aGlyph;
    SDL_Texture *bGlyph;

    dpadLeftGlyph = GC_LoadCarouselControllerGlyph(
        renderer,
        &gcCarouselDpadLeftGlyph,
        CH_CONTROLLER_GLYPH_DPAD_LEFT,
        GC_CAROUSEL_NAV_GLYPH_SIZE,
        "D-pad left");

    dpadRightGlyph = GC_LoadCarouselControllerGlyph(
        renderer,
        &gcCarouselDpadRightGlyph,
        CH_CONTROLLER_GLYPH_DPAD_RIGHT,
        GC_CAROUSEL_NAV_GLYPH_SIZE,
        "D-pad right");

    aGlyph = GC_LoadCarouselControllerGlyph(
        renderer,
        &gcCarouselAGlyph,
        CH_CONTROLLER_GLYPH_A,
        GC_CAROUSEL_ACTION_GLYPH_SIZE,
        "A");

    bGlyph = GC_LoadCarouselControllerGlyph(
        renderer,
        &gcCarouselBGlyph,
        CH_CONTROLLER_GLYPH_B,
        GC_CAROUSEL_ACTION_GLYPH_SIZE,
        "B");


    SDL_Rect fullscreen =
    {
        0,
        0,
        GC_LAUNCHER_WIDTH,
        480
    };

    SDL_SetRenderDrawColor(
        renderer,
        0,
        0,
        0,
        255);

    SDL_RenderClear(renderer);

    if (selected >= 0 &&
        selected < GC_LAUNCHER_ENTRY_COUNT &&
        GC_LauncherEntryArtPath(selected) != NULL)
    {
        background =
            GC_LoadLauncherBitmap(
                renderer,
                GC_LauncherEntryArtPath(selected),
                "TITLEPIC");

        titlepicLoaded =
            background != NULL;
    }

    if (background == NULL)
    {
        background =
            GC_LoadLauncherLogo(
                renderer);
    }

    if (background != NULL)
    {
        if (titlepicLoaded)
        {
            if (SDL_RenderCopy(
                    renderer,
                    background,
                    NULL,
                    &fullscreen) != 0)
            {
                DC_WARN(
                    "DoomCube: TITLEPIC render failed: %s\n",
                    SDL_GetError());
            }
        }
        else
        {
            int logoWidth;
            int logoHeight;

            if (SDL_QueryTexture(
                    background,
                    NULL,
                    NULL,
                    &logoWidth,
                    &logoHeight) == 0)
            {
                SDL_Rect logoRect =
                {
                    (GC_LAUNCHER_WIDTH - logoWidth) / 2,
                    (286 - logoHeight) / 2,
                    logoWidth,
                    logoHeight
                };

                if (logoRect.x < 0)
                    logoRect.x = 0;

                if (logoRect.y < 0)
                    logoRect.y = 0;

                if (SDL_RenderCopy(
                        renderer,
                        background,
                        NULL,
                        &logoRect) != 0)
                {
                    DC_WARN(
                        "DoomCube: fallback logo render failed: %s\n",
                        SDL_GetError());
                }
            }
        }
    }

    /*
     * Deliberately no lower black overlay.
     * Let the TITLEPIC occupy the complete 640x480 presentation.
     */

    SDL_SetRenderDrawColor(
        renderer,
        255,
        255,
        255,
        255);

    textWidth =
        GC_TextWidth(
            GC_LauncherEntryName(selected),
            5);

    GC_DrawText(
        renderer,
        (GC_LAUNCHER_WIDTH - textWidth) / 2,
        294,
        GC_LauncherEntryName(selected),
        5);

    previous =
        GC_NextAvailableEntry(
            selected,
            -1);

    next =
        GC_NextAvailableEntry(
            selected,
            1);

    if (previous >= 0 &&
        previous != selected)
    {
        int width =
            GC_TextWidth(
                GC_LauncherEntryName(previous),
                3);

        GC_DrawText(
            renderer,
            160 - width / 2,
            360,
            GC_LauncherEntryName(previous),
            3);

        if (dpadLeftGlyph != NULL)
        {
            GC_DrawCarouselControllerGlyph(
                renderer,
                dpadLeftGlyph,
                160 - width / 2
                    - GC_CAROUSEL_NAV_GLYPH_GAP
                    - GC_CAROUSEL_NAV_GLYPH_SIZE,
                GC_CAROUSEL_NAV_GLYPH_Y,
                GC_CAROUSEL_NAV_GLYPH_SIZE);
        }
    }

    if (next >= 0 &&
        next != selected)
    {
        int width =
            GC_TextWidth(
                GC_LauncherEntryName(next),
                3);

        GC_DrawText(
            renderer,
            480 - width / 2,
            360,
            GC_LauncherEntryName(next),
            3);

        if (dpadRightGlyph != NULL)
        {
            GC_DrawCarouselControllerGlyph(
                renderer,
                dpadRightGlyph,
                480 + width / 2
                    + GC_CAROUSEL_NAV_GLYPH_GAP,
                GC_CAROUSEL_NAV_GLYPH_Y,
                GC_CAROUSEL_NAV_GLYPH_SIZE);
        }
    }

    {
        const char *selectText =
            "LEFT RIGHT - SELECT";

        const char *actionText =
            "A - START    B - BACK";

        /*
         * D-pad glyphs beside the neighbouring titles teach
         * horizontal navigation directly. Keep the old prose only
         * as a fallback if either SVG cannot be loaded.
         */
        if (dpadLeftGlyph == NULL ||
            dpadRightGlyph == NULL)
        {
            GC_DrawText(
                renderer,
                (GC_LAUNCHER_WIDTH -
                    GC_TextWidth(selectText, 3)) / 2,
                414,
                selectText,
                3);
        }

        if (aGlyph != NULL &&
            bGlyph != NULL)
        {
            const char *startText = "START";
            const char *backText = "BACK";

            const int startWidth =
                GC_TextWidth(startText, 3);

            const int backWidth =
                GC_TextWidth(backText, 3);

            const int totalWidth =
                GC_CAROUSEL_ACTION_GLYPH_SIZE
                + GC_CAROUSEL_ACTION_GLYPH_GAP
                + startWidth
                + GC_CAROUSEL_ACTION_PAIR_GAP
                + GC_CAROUSEL_ACTION_GLYPH_SIZE
                + GC_CAROUSEL_ACTION_GLYPH_GAP
                + backWidth;

            int x =
                (GC_LAUNCHER_WIDTH - totalWidth) / 2;

            GC_DrawCarouselControllerGlyph(
                renderer,
                aGlyph,
                x,
                GC_CAROUSEL_ACTION_GLYPH_Y,
                GC_CAROUSEL_ACTION_GLYPH_SIZE);

            x +=
                GC_CAROUSEL_ACTION_GLYPH_SIZE
                + GC_CAROUSEL_ACTION_GLYPH_GAP;

            GC_DrawText(
                renderer,
                x,
                GC_CAROUSEL_ACTION_TEXT_Y,
                startText,
                3);

            x +=
                startWidth
                + GC_CAROUSEL_ACTION_PAIR_GAP;

            GC_DrawCarouselControllerGlyph(
                renderer,
                bGlyph,
                x,
                GC_CAROUSEL_ACTION_GLYPH_Y,
                GC_CAROUSEL_ACTION_GLYPH_SIZE);

            x +=
                GC_CAROUSEL_ACTION_GLYPH_SIZE
                + GC_CAROUSEL_ACTION_GLYPH_GAP;

            GC_DrawText(
                renderer,
                x,
                GC_CAROUSEL_ACTION_TEXT_Y,
                backText,
                3);
        }
        else
        {
            GC_DrawText(
                renderer,
                (GC_LAUNCHER_WIDTH -
                    GC_TextWidth(actionText, 3)) / 2,
                448,
                actionText,
                3);
        }
    }

    SDL_RenderPresent(renderer);

    if (background != NULL)
    {
        SDL_DestroyTexture(
            background);
    }
}


#define GC_CUSTOM_ACTION_GLYPH_SIZE 36
#define GC_CUSTOM_ACTION_GLYPH_GAP   8
#define GC_CUSTOM_ACTION_PAIR_GAP   28
#define GC_CUSTOM_ACTION_GLYPH_Y   397
#define GC_CUSTOM_ACTION_TEXT_Y    410


static int GC_DrawCustomActionHints(
    SDL_Renderer *renderer,
    const char *primaryText)
{
    SDL_Texture *aGlyph;
    SDL_Texture *bGlyph;
    const char *backText = "BACK";
    int primaryWidth;
    int backWidth;
    int totalWidth;
    int x;

    aGlyph =
        GC_LoadCarouselControllerGlyph(
            renderer,
            &gcCarouselAGlyph,
            CH_CONTROLLER_GLYPH_A,
            GC_CUSTOM_ACTION_GLYPH_SIZE,
            "A");

    bGlyph =
        GC_LoadCarouselControllerGlyph(
            renderer,
            &gcCarouselBGlyph,
            CH_CONTROLLER_GLYPH_B,
            GC_CUSTOM_ACTION_GLYPH_SIZE,
            "B");

    if (aGlyph == NULL ||
        bGlyph == NULL)
    {
        return 0;
    }

    primaryWidth = GC_TextWidth(primaryText, 3);
    backWidth = GC_TextWidth(backText, 3);

    totalWidth =
        GC_CUSTOM_ACTION_GLYPH_SIZE
        + GC_CUSTOM_ACTION_GLYPH_GAP
        + primaryWidth
        + GC_CUSTOM_ACTION_PAIR_GAP
        + GC_CUSTOM_ACTION_GLYPH_SIZE
        + GC_CUSTOM_ACTION_GLYPH_GAP
        + backWidth;

    x = (GC_LAUNCHER_WIDTH - totalWidth) / 2;

    GC_DrawCarouselControllerGlyph(
        renderer,
        aGlyph,
        x,
        GC_CUSTOM_ACTION_GLYPH_Y,
        GC_CUSTOM_ACTION_GLYPH_SIZE);

    x += GC_CUSTOM_ACTION_GLYPH_SIZE
        + GC_CUSTOM_ACTION_GLYPH_GAP;

    GC_DrawText(
        renderer,
        x,
        GC_CUSTOM_ACTION_TEXT_Y,
        primaryText,
        3);

    x += primaryWidth + GC_CUSTOM_ACTION_PAIR_GAP;

    GC_DrawCarouselControllerGlyph(
        renderer,
        bGlyph,
        x,
        GC_CUSTOM_ACTION_GLYPH_Y,
        GC_CUSTOM_ACTION_GLYPH_SIZE);

    x += GC_CUSTOM_ACTION_GLYPH_SIZE
        + GC_CUSTOM_ACTION_GLYPH_GAP;

    GC_DrawText(
        renderer,
        x,
        GC_CUSTOM_ACTION_TEXT_Y,
        backText,
        3);

    return 1;
}


static void GC_DrawCustomBaseLauncher(
    SDL_Renderer *renderer,
    int selected)
{
    const char *title = "SELECT BASE";
    const char *controls = "A - SELECT    B - BACK";

    int i;
    int shown = 0;

    SDL_SetRenderDrawColor(
        renderer,
        0,
        0,
        0,
        255);

    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(
        renderer,
        255,
        255,
        255,
        255);

    GC_DrawText(
        renderer,
        (GC_LAUNCHER_WIDTH -
            GC_TextWidth(title, 4)) / 2,
        65,
        title,
        4);

    for (i = 0; i < GC_CUSTOM_BASE_COUNT; ++i)
    {
        int y;
        int textWidth;

        if (!GC_CustomBaseAvailable(i))
            continue;

        y =
            155 +
            shown * GC_LAUNCHER_LINE_HEIGHT;



        textWidth =
            GC_TextWidth(
                gcCustomBases[i].name,
                3);

        if (i == selected &&
            !GC_DrawDoomMenuSkullCursor(
                renderer,
                (GC_LAUNCHER_WIDTH - textWidth) / 2,
                y))
        {
            GC_DrawCustomSelectionFallback(
                renderer,
                y);
        }

        GC_DrawText(
            renderer,
            (GC_LAUNCHER_WIDTH -
                textWidth) / 2,
            y,
            gcCustomBases[i].name,
            3);

        ++shown;
    }

    if (!GC_DrawCustomActionHints(
            renderer,
            "SELECT"))
    {
        GC_DrawText(
            renderer,
            (GC_LAUNCHER_WIDTH -
                GC_TextWidth(controls, 3)) / 2,
            410,
            controls,
            3);
    };

    SDL_RenderPresent(renderer);
}

static void GC_LauncherMenuChoosePlay(void);


static int GC_LauncherRunCustomBase(
    SDL_Renderer *renderer)
{
    int selected;
    int stickHeld = 0;

    selected =
        GC_FirstAvailableCustomBase();

    if (selected < 0)
        return -1;

    GC_DrawCustomBaseLauncher(
        renderer,
        selected);

    /*
     * Flush stale button transitions before entering the submenu.
     */
    for (int i = 0; i < 3; ++i)
    {
        PAD_ScanPads();
        (void)PAD_ButtonsDown(0);
        SDL_Delay(16);
    }

    for (;;)
    {
        u32 down;
        int stickY;
        int stickDirection = 0;

        PAD_ScanPads();

        down =
            PAD_ButtonsDown(0);

        stickY =
            PAD_StickY(0);

        if (stickY > GC_LAUNCHER_DEADZONE)
            stickDirection = -1;
        else if (stickY < -GC_LAUNCHER_DEADZONE)
            stickDirection = 1;

        if ((down & PAD_BUTTON_UP) ||
            (stickDirection < 0 && !stickHeld))
        {
            int next =
                GC_NextAvailableCustomBase(
                    selected,
                    -1);

            if (next >= 0)
            {
                selected = next;

                GC_DrawCustomBaseLauncher(
                    renderer,
                    selected);
            }
        }

        if ((down & PAD_BUTTON_DOWN) ||
            (stickDirection > 0 && !stickHeld))
        {
            int next =
                GC_NextAvailableCustomBase(
                    selected,
                    1);

            if (next >= 0)
            {
                selected = next;

                GC_DrawCustomBaseLauncher(
                    renderer,
                    selected);
            }
        }

        stickHeld =
            stickDirection != 0;

        if (down & PAD_BUTTON_B)
        {
            DC_DEBUG(
                "DoomCube: CUSTOM base selection cancelled\\n");

            return -2;
        }

        if (down &
            (PAD_BUTTON_A |
             PAD_BUTTON_START))
        {
            DC_DEBUG(
                "DoomCube: CUSTOM base selected %s\\n",
                gcCustomBases[selected].name);

            GC_LauncherMenuChoosePlay();

            return selected;
        }

        SDL_Delay(16);
    }
}


static void GC_DrawCustomPwadLauncher(
    SDL_Renderer *renderer,
    int baseIndex,
    int selected)
{
    const char *title = "SELECT PWAD";
    const char *controls =
        "A - START    B - BACK";

    const int visibleRows = 7;

    int total;
    int selectedOrdinal;
    int first;
    int last;
    int ordinal;
    int i;

    if (baseIndex < 0 ||
        baseIndex >= GC_CUSTOM_BASE_COUNT)
    {
        return;
    }

    total =
        GC_CustomPwadCount(
            baseIndex);

    selectedOrdinal =
        GC_CustomPwadOrdinal(
            baseIndex,
            selected);

    first = 0;

    if (selectedOrdinal >= visibleRows)
    {
        first =
            selectedOrdinal -
            visibleRows +
            1;
    }

    last =
        first +
        visibleRows;

    SDL_SetRenderDrawColor(
        renderer,
        0,
        0,
        0,
        255);

    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(
        renderer,
        255,
        255,
        255,
        255);

    GC_DrawText(
        renderer,
        (GC_LAUNCHER_WIDTH -
            GC_TextWidth(title, 4)) / 2,
        45,
        title,
        4);

    GC_DrawText(
        renderer,
        (GC_LAUNCHER_WIDTH -
            GC_TextWidth(
                gcCustomBases[baseIndex].name,
                3)) / 2,
        100,
        gcCustomBases[baseIndex].name,
        3);

    ordinal = 0;

    for (i = 0; i < gcAvailablePwadCount; ++i)
    {
        int y;
        char displayName[48];
        int textWidth;

        if (gcPwads[i].baseIndex != baseIndex)
        {
            continue;
        }

        if (ordinal < first ||
            ordinal >= last)
        {
            ++ordinal;
            continue;
        }

        y =
            145 +
            (ordinal - first) *
                GC_LAUNCHER_LINE_HEIGHT;



        /*
         * Keep pathological filenames inside the safe width of
         * this temporary built-in-font menu.
         */
        snprintf(
            displayName,
            sizeof(displayName),
            "%s",
            gcPwads[i].name);

        /* Fit larger scale-3 text inside the safe menu width. */
        while (displayName[0] != '\0' &&
               GC_TextWidth(displayName, 3) > 500)
        {
            size_t len = strlen(displayName);

            if (len == 0)
                break;

            displayName[len - 1] = '\0';
        }

        textWidth =
            GC_TextWidth(
                displayName,
                3);

        if (i == selected &&
            !GC_DrawDoomMenuSkullCursor(
                renderer,
                (GC_LAUNCHER_WIDTH - textWidth) / 2,
                y))
        {
            GC_DrawCustomSelectionFallback(
                renderer,
                y);
        }

        GC_DrawText(
            renderer,
            (GC_LAUNCHER_WIDTH -
                textWidth) / 2,
            y,
            displayName,
            3);

        ++ordinal;
    }

    if (total > visibleRows)
    {
        char position[32];

        snprintf(
            position,
            sizeof(position),
            "%d / %d",
            selectedOrdinal + 1,
            total);

        GC_DrawText(
            renderer,
            (GC_LAUNCHER_WIDTH -
                GC_TextWidth(position, 2)) / 2,
            380,
            position,
            2);
    }

    if (!GC_DrawCustomActionHints(
            renderer,
            "START"))
    {
        GC_DrawText(
            renderer,
            (GC_LAUNCHER_WIDTH -
                GC_TextWidth(controls, 3)) / 2,
            410,
            controls,
            3);
    };

    SDL_RenderPresent(renderer);
}


static int GC_LauncherRunCustomPwad(
    SDL_Renderer *renderer,
    int baseIndex)
{
    int selected;
    int stickHeld = 0;

    selected =
        GC_FirstCustomPwad(
            baseIndex);

    if (selected < 0)
    {
        return -1;
    }

    GC_DrawCustomPwadLauncher(
        renderer,
        baseIndex,
        selected);

    /*
     * Consume the A/START transition that entered this submenu.
     */
    for (int i = 0; i < 3; ++i)
    {
        PAD_ScanPads();
        (void)PAD_ButtonsDown(0);
        SDL_Delay(16);
    }

    DC_DEBUG(
        "DoomCube: entering CUSTOM PWAD selection for %s\n",
        gcCustomBases[baseIndex].name);

    for (;;)
    {
        u32 down;
        int stickY;
        int stickDirection = 0;

        PAD_ScanPads();

        down =
            PAD_ButtonsDown(0);

        stickY =
            PAD_StickY(0);

        if (stickY > GC_LAUNCHER_DEADZONE)
        {
            stickDirection = -1;
        }
        else if (stickY < -GC_LAUNCHER_DEADZONE)
        {
            stickDirection = 1;
        }

        if ((down & PAD_BUTTON_UP) ||
            (stickDirection < 0 &&
             !stickHeld))
        {
            int previous =
                GC_NextCustomPwad(
                    selected,
                    baseIndex,
                    -1);

            if (previous >= 0)
            {
                selected =
                    previous;

                GC_DrawCustomPwadLauncher(
                    renderer,
                    baseIndex,
                    selected);
            }
        }

        if ((down & PAD_BUTTON_DOWN) ||
            (stickDirection > 0 &&
             !stickHeld))
        {
            int next =
                GC_NextCustomPwad(
                    selected,
                    baseIndex,
                    1);

            if (next >= 0)
            {
                selected =
                    next;

                GC_DrawCustomPwadLauncher(
                    renderer,
                    baseIndex,
                    selected);
            }
        }

        stickHeld =
            stickDirection != 0;

        if (down & PAD_BUTTON_B)
        {
            DC_DEBUG(
                "DoomCube: CUSTOM PWAD selection cancelled\n");

            return -2;
        }

        if (down &
            (PAD_BUTTON_A |
             PAD_BUTTON_START))
        {
            DC_DEBUG(
                "DoomCube: CUSTOM PWAD selected %s\n",
                gcPwads[selected].path);

            GC_LauncherMenuChoosePlay();

            return selected;
        }

        SDL_Delay(16);
    }
}


static void GC_DrawLoadingOverlay(SDL_Renderer *renderer)
{
    const char *text = "LOADING";
    const int textScale =
        GC_LAUNCHER_FONT_SCALE + 2;
    int textWidth;

    textWidth =
        GC_TextWidth(
            text,
            textScale);

    GC_DrawText(
        renderer,
        (GC_LAUNCHER_WIDTH - textWidth) / 2,
        228,
        text,
        textScale);

    SDL_RenderPresent(renderer);
}


#define GC_SPLASH_START_GLYPH_RASTER_SIZE 72
#define GC_SPLASH_START_GLYPH_VISUAL_SLOT 31
#define GC_SPLASH_START_GLYPH_Y_OFFSET    12

static SDL_Texture *gcSplashStartGlyphTexture;
static bool gcSplashStartGlyphLoadAttempted;

static SDL_Texture *GC_LoadSplashStartGlyph(
    SDL_Renderer *renderer)
{
    if (gcSplashStartGlyphTexture != NULL)
        return gcSplashStartGlyphTexture;

    if (gcSplashStartGlyphLoadAttempted)
        return NULL;

    gcSplashStartGlyphLoadAttempted = true;

    gcSplashStartGlyphTexture =
        CH_ControllerGlyphSDLLoadTexture(
            renderer,
            "dvd:/",
            CH_CONTROLLER_GLYPH_START,
            GC_SPLASH_START_GLYPH_RASTER_SIZE);

    if (gcSplashStartGlyphTexture == NULL)
    {
        fprintf(
            stderr,
            "DoomCube: START controller glyph unavailable: %s\n",
            SDL_GetError());
    }

    return gcSplashStartGlyphTexture;
}

static void GC_DrawSplashStartGlyph(
    SDL_Renderer *renderer,
    SDL_Texture *texture,
    int x,
    int y)
{
    Uint8 red = 255;
    Uint8 green = 255;
    Uint8 blue = 255;
    Uint8 alpha = 255;
    SDL_Rect dst;

    if (renderer == NULL || texture == NULL)
        return;

    /*
     * Step 8's Doom font reads the current renderer alpha. Read the same
     * state here so the SVG follows the exact PRESS START triangle fade.
     */
    if (SDL_GetRenderDrawColor(
            renderer,
            &red,
            &green,
            &blue,
            &alpha) != 0)
    {
        alpha = 255;
    }

    SDL_SetTextureAlphaMod(
        texture,
        alpha);

    dst.x =
        x
        + ((GC_SPLASH_START_GLYPH_VISUAL_SLOT
            - GC_SPLASH_START_GLYPH_RASTER_SIZE) / 2);

    dst.y =
        y
        + GC_SPLASH_START_GLYPH_Y_OFFSET;

    dst.w = GC_SPLASH_START_GLYPH_RASTER_SIZE;
    dst.h = GC_SPLASH_START_GLYPH_RASTER_SIZE;

    SDL_RenderCopy(
        renderer,
        texture,
        NULL,
        &dst);
}

static void GC_LauncherControllerGlyphShutdown(void)
{
    if (gcSplashStartGlyphTexture != NULL)
    {
        SDL_DestroyTexture(
            gcSplashStartGlyphTexture);

        gcSplashStartGlyphTexture = NULL;
    }

    gcSplashStartGlyphLoadAttempted = false;

    if (gcCarouselDpadLeftGlyph != NULL)
    {
        SDL_DestroyTexture(gcCarouselDpadLeftGlyph);
        gcCarouselDpadLeftGlyph = NULL;
    }

    if (gcCarouselDpadRightGlyph != NULL)
    {
        SDL_DestroyTexture(gcCarouselDpadRightGlyph);
        gcCarouselDpadRightGlyph = NULL;
    }

    if (gcCarouselAGlyph != NULL)
    {
        SDL_DestroyTexture(gcCarouselAGlyph);
        gcCarouselAGlyph = NULL;
    }

    if (gcCarouselBGlyph != NULL)
    {
        SDL_DestroyTexture(gcCarouselBGlyph);
        gcCarouselBGlyph = NULL;
    }

}

static void GC_DrawSplash(
    SDL_Renderer *renderer,
    SDL_Texture *fallbackLogo,
    SDL_Texture *collage,
    SDL_Texture *doomMenuLogo,
    SDL_Texture *splashCube,
    Uint8 promptAlpha)
{
    const char *prompt =
        "PRESS START";

    const char *copyleft =
        "@ COPYLEFT 2026 SPERGE BRIGADE STUDIOS";

    char versionText[96];
    int versionWidth;
    SDL_Rect doomLogoRect =
    {
        0,
        0,
        0,
        0
    };
    bool doomLogoRectValid = false;

    SDL_SetRenderDrawColor(
        renderer,
        0,
        0,
        0,
        255);

    SDL_RenderClear(renderer);

    if (collage != NULL)
    {
        SDL_Rect fullscreen =
        {
            0,
            0,
            GC_LAUNCHER_WIDTH,
            480
        };

        SDL_RenderCopy(
            renderer,
            collage,
            NULL,
            &fullscreen);
    }

    if (doomMenuLogo != NULL)
    {
        int width;
        int height;

        if (SDL_QueryTexture(
                doomMenuLogo,
                NULL,
                NULL,
                &width,
                &height) == 0)
        {
            /*
             * M_DOOM is only 123x60 in the IWAD.
             *
             * Keep this at an exact integer 3x scale.  The previous
             * ~590px-wide experiment pushed the tiny source art too far
             * and made the individual source pixels dominate.
             */
            doomLogoRect.x = 0;
            doomLogoRect.y = 30;
            doomLogoRect.w = width * 3;
            doomLogoRect.h = height * 3;

            doomLogoRect.x =
                (GC_LAUNCHER_WIDTH - doomLogoRect.w) / 2;

            doomLogoRectValid = true;

            SDL_RenderCopy(
                renderer,
                doomMenuLogo,
                NULL,
                &doomLogoRect);
        }
    }

    {
        SDL_Texture *cube =
            splashCube != NULL
            ? splashCube
            : fallbackLogo;

        if (cube != NULL)
        {
            int logoWidth;
            int logoHeight;

            if (SDL_QueryTexture(
                    cube,
                    NULL,
                    NULL,
                    &logoWidth,
                    &logoHeight) == 0)
            {
                SDL_Rect logoRect =
                {
                    0,
                    0,
                    logoWidth,
                    logoHeight
                };

                /*
                 * Centre the cube on M_DOOM and pull it substantially
                 * upward into the notch beneath the two O's.
                 *
                 * With a 106px cube and 85px overlap, only ~38px hangs
                 * below the DOOM logo.  It should read as one combined
                 * mark rather than two stacked logos.
                 */
                if (logoRect.w > 106)
                {
                    logoRect.h =
                        logoRect.h * 106 / logoRect.w;

                    logoRect.w = 106;
                }

                if (logoRect.h > 106)
                {
                    logoRect.w =
                        logoRect.w * 106 / logoRect.h;

                    logoRect.h = 106;
                }

                if (doomLogoRectValid)
                {
                    logoRect.x =
                        doomLogoRect.x
                        + ((doomLogoRect.w - logoRect.w) / 2) - 5;

                    logoRect.y =
                        doomLogoRect.y
                        + doomLogoRect.h
                        -74;
                }
                else
                {
                    logoRect.x =
                        (GC_LAUNCHER_WIDTH - logoRect.w) / 2;

                    logoRect.y = 142;
                }

                SDL_RenderCopy(
                    renderer,
                    cube,
                    NULL,
                    &logoRect);
            }
        }
    }

    /*
     * Native STCFN RGB is preserved. Only alpha changes.
     * Pulse between 64 and 255 over a 1.6 second triangle wave.
     */
    SDL_SetRenderDrawColor(
        renderer,
        255,
        255,
        255,
        promptAlpha);

    {
        SDL_Texture *startGlyph =
            GC_LoadSplashStartGlyph(renderer);

        if (startGlyph != NULL)
        {
            const int startTextWidth =
                GC_TextWidth(
                    prompt,
                    3);

            const int startTextX =
                (GC_LAUNCHER_WIDTH -
                    startTextWidth) / 2;

            const int startGlyphX =
                startTextX
                + ((startTextWidth -
                    GC_SPLASH_START_GLYPH_VISUAL_SLOT) / 2);

            GC_DrawText(
                renderer,
                startTextX,
                350,
                prompt,
                3);

            GC_DrawSplashStartGlyph(
                renderer,
                startGlyph,
                startGlyphX,
                350);
        }
        else
        {
            GC_DrawText(
                    renderer,
                    (GC_LAUNCHER_WIDTH -
                        GC_TextWidth(prompt, 3)) / 2,
                    338,
                    prompt,
                    3);
        }
    }

    SDL_SetRenderDrawColor(
        renderer,
        255,
        255,
        255,
        255);

    GC_DrawText(
        renderer,
        (GC_LAUNCHER_WIDTH -
            GC_TextWidth(copyleft, 2)) / 2,
        420,
        copyleft,
        2);

    snprintf(
        versionText,
        sizeof(versionText),
        "DOOMCUBE V%s (%s)",
        DOOMCUBE_APP_VERSION,
        DOOMCUBE_GIT_ID);

    versionWidth =
        GC_TextWidth(
            versionText,
            1);

    GC_DrawText(
        renderer,
        (GC_LAUNCHER_WIDTH - versionWidth) / 2,
        455,
        versionText,
        1);

    SDL_RenderPresent(renderer);
}


typedef struct GC_LauncherMusicState
{
    Mix_Music *music;
    const char *currentPath;
    bool ready;
    bool mixInitCalled;
    bool openedMixer;
    bool initializedAudio;
} GC_LauncherMusicState;


static GC_LauncherMusicState gcLauncherMusic;



typedef struct GC_LauncherMusicCacheEntry
{
    const char *path;
    Mix_Music *music;
} GC_LauncherMusicCacheEntry;


static GC_LauncherMusicCacheEntry gcLauncherMusicCache[] =
{
    { GC_LAUNCHER_MUSIC_DOOM1_PATH, NULL },
    { GC_LAUNCHER_MUSIC_DOOM_PATH, NULL },
    { GC_LAUNCHER_MUSIC_DOOM2_PATH, NULL },
    { GC_LAUNCHER_MUSIC_TNT_PATH, NULL },
    { GC_LAUNCHER_MUSIC_PLUTONIA_PATH, NULL },
    { GC_LAUNCHER_MUSIC_SIGIL_PATH, NULL },
    { GC_LAUNCHER_MUSIC_SIGIL2_PATH, NULL },
    { GC_LAUNCHER_MUSIC_CUSTOM_PATH, NULL }
};

#define GC_LAUNCHER_MUSIC_CACHE_COUNT \
    ((int)(sizeof(gcLauncherMusicCache) / sizeof(gcLauncherMusicCache[0])))




static void GC_LauncherMusicStopCurrent(void)
{
    if (gcLauncherMusic.music != NULL)
    {
        Mix_HaltMusic();
    }

    gcLauncherMusic.music = NULL;
    gcLauncherMusic.currentPath = NULL;
}



static void GC_LauncherMenuChooseShutdown(bool waitForPlayback);

static void GC_LauncherMusicShutdown(void)
{
    int i;

    GC_LauncherMenuChooseShutdown(true);

    GC_LauncherMusicStopCurrent();

    for (i = 0;
         i < GC_LAUNCHER_MUSIC_CACHE_COUNT;
         ++i)
    {
        if (gcLauncherMusicCache[i].music != NULL)
        {
            Mix_FreeMusic(
                gcLauncherMusicCache[i].music);

            gcLauncherMusicCache[i].music = NULL;
        }
    }

    if (gcLauncherMusic.openedMixer)
    {
        Mix_CloseAudio();
        gcLauncherMusic.openedMixer = false;
    }

    if (gcLauncherMusic.initializedAudio)
    {
        SDL_QuitSubSystem(
            SDL_INIT_AUDIO);

        gcLauncherMusic.initializedAudio = false;
    }

    if (gcLauncherMusic.mixInitCalled)
    {
        Mix_Quit();
        gcLauncherMusic.mixInitCalled = false;
    }

    if (gcLauncherMusic.ready)
    {
        DC_DEBUG(
            "DoomCube: launcher music shutdown complete\n");
    }

    gcLauncherMusic.ready = false;
}



static bool GC_LauncherMusicEnsureReady(void)
{
    int frequency;
    int channels;
    Uint16 format;
    int mixerRate;
    const char *cfg;

    if (gcLauncherMusic.ready)
        return true;

    Mix_Init(0);
    gcLauncherMusic.mixInitCalled = true;

    if ((SDL_WasInit(SDL_INIT_AUDIO)
         & SDL_INIT_AUDIO) == 0)
    {
        if (SDL_InitSubSystem(
                SDL_INIT_AUDIO) < 0)
        {
            DC_WARN(
                "DoomCube: launcher music SDL audio init failed: %s\n",
                SDL_GetError());

            GC_LauncherMusicShutdown();
            return false;
        }

        gcLauncherMusic.initializedAudio = true;
    }

    if (!Mix_QuerySpec(
            &frequency,
            &format,
            &channels))
    {
        mixerRate =
            snd_samplerate > 0
            ? snd_samplerate
            : 44100;

        if (Mix_OpenAudio(
                mixerRate,
                AUDIO_S16SYS,
                2,
                1024) < 0)
        {
            DC_WARN(
                "DoomCube: launcher music Mix_OpenAudio failed: %s\n",
                Mix_GetError());

            GC_LauncherMusicShutdown();
            return false;
        }

        gcLauncherMusic.openedMixer = true;

        DC_DEBUG(
            "DoomCube: launcher music opened mixer at %d Hz\n",
            mixerRate);
    }
    else
    {
        DC_DEBUG(
            "DoomCube: launcher music using existing mixer "
            "freq=%d channels=%d format=0x%x\n",
            frequency,
            channels,
            (unsigned int)format);
    }

    if (!Mix_SetTimidityCfg(
            GC_LAUNCHER_TIMIDITY_CFG))
    {
        DC_WARN(
            "DoomCube: launcher music Mix_SetTimidityCfg failed: %s\n",
            Mix_GetError());

        GC_LauncherMusicShutdown();
        return false;
    }

    cfg =
        Mix_GetTimidityCfg();

    DC_DEBUG(
        "DoomCube: launcher music TiMidity config: %s\n",
        cfg != NULL ? cfg : "(null)");

    Mix_VolumeMusic(
        MIX_MAX_VOLUME);

    gcLauncherMusic.ready = true;

    DC_INFO(
        "DoomCube: launcher MIDI player ready\n");

    return true;
}


static GC_LauncherMusicCacheEntry *GC_LauncherMusicFindCache(
    const char *path)
{
    int i;

    if (path == NULL)
    {
        return NULL;
    }

    for (i = 0;
         i < GC_LAUNCHER_MUSIC_CACHE_COUNT;
         ++i)
    {
        if (strcmp(
                gcLauncherMusicCache[i].path,
                path) == 0)
        {
            return
                &gcLauncherMusicCache[i];
        }
    }

    return NULL;
}


static Mix_Music *GC_LauncherMusicLoadCached(
    const char *path)
{
    GC_LauncherMusicCacheEntry *entry;

    entry =
        GC_LauncherMusicFindCache(
            path);

    if (entry == NULL)
    {
        DC_WARN(
            "DoomCube: launcher music has no cache slot for %s\n",
            path != NULL ? path : "(null)");

        return NULL;
    }

    if (entry->music != NULL)
    {
        return
            entry->music;
    }

    entry->music =
        Mix_LoadMUS(
            path);

    if (entry->music == NULL)
    {
        DC_WARN(
            "DoomCube: launcher music load failed for %s: %s\n",
            path,
            Mix_GetError());

        return NULL;
    }

    DC_DEBUG(
        "DoomCube: launcher music cached %s\n",
        path);

    return
        entry->music;
}


static bool GC_LauncherMusicUsePath(
    const char *path)
{
    Mix_Music *music;

    if (path == NULL)
    {
        return false;
    }

    if (!GC_LauncherMusicEnsureReady())
    {
        return false;
    }

    if (gcLauncherMusic.music != NULL
        && gcLauncherMusic.currentPath != NULL
        && strcmp(
            gcLauncherMusic.currentPath,
            path) == 0)
    {
        if (Mix_PlayingMusic())
        {
            DC_TRACE(
                "DoomCube: launcher music unchanged: %s\n",
                path);

            return true;
        }

        if (Mix_PlayMusic(
                gcLauncherMusic.music,
                -1) == 0)
        {
            DC_DEBUG(
                "DoomCube: launcher music resumed/restarted: %s\n",
                path);

            return true;
        }

        DC_WARN(
            "DoomCube: launcher music replay failed for %s: %s\n",
            path,
            Mix_GetError());

        GC_LauncherMusicStopCurrent();
    }
    else
    {
        GC_LauncherMusicStopCurrent();
    }

    music =
        GC_LauncherMusicLoadCached(
            path);

    if (music == NULL)
    {
        return false;
    }

    if (Mix_PlayMusic(
            music,
            -1) < 0)
    {
        DC_WARN(
            "DoomCube: launcher music playback failed for %s: %s\n",
            path,
            Mix_GetError());

        return false;
    }

    gcLauncherMusic.music = music;
    gcLauncherMusic.currentPath = path;

    DC_INFO(
        "DoomCube: launcher music now playing %s\n",
        path);

    return true;
}



static bool GC_LauncherMusicUseIntermission(void)
{
    return GC_LauncherMusicUsePath(
        GC_LAUNCHER_MUSIC_CUSTOM_PATH);
}


static const char *GC_LauncherMusicPathForGame(
    int gameIndex)
{
    switch (gameIndex)
    {
        case 0:
            return GC_LAUNCHER_MUSIC_DOOM1_PATH;

        case 1:
            return GC_LAUNCHER_MUSIC_DOOM_PATH;

        case 2:
            return GC_LAUNCHER_MUSIC_DOOM2_PATH;

        case 3:
            return GC_LAUNCHER_MUSIC_TNT_PATH;

        case 4:
            return GC_LAUNCHER_MUSIC_PLUTONIA_PATH;

        case 5:
            return GC_LAUNCHER_MUSIC_SIGIL_PATH;

        case 6:
            return GC_LAUNCHER_MUSIC_SIGIL2_PATH;

        case GC_CUSTOM_GAME_INDEX:
            return GC_LAUNCHER_MUSIC_CUSTOM_PATH;

        default:
            return NULL;
    }
}


static bool GC_LauncherMusicUseGame(
    int gameIndex)
{
    const char *path =
        GC_LauncherMusicPathForGame(
            gameIndex);

    if (path == NULL)
    {
        DC_WARN(
            "DoomCube: no launcher music mapping for game index %d\n",
            gameIndex);

        return false;
    }

    return GC_LauncherMusicUsePath(
        path);
}


static void GC_LauncherMusicPreloadAvailable(void)
{
    int gameIndex;
    int loaded = 0;
    int alreadyLoaded = 0;
    int failed = 0;

    if (!GC_LauncherMusicEnsureReady())
    {
        DC_WARN(
            "DoomCube: launcher music preload skipped: MIDI player unavailable\n");

        return;
    }

    for (gameIndex = 0;
         gameIndex < GC_MAX_GAMES;
         ++gameIndex)
    {
        const char *path;
        GC_LauncherMusicCacheEntry *entry;

        if (gameIndex != GC_CUSTOM_GAME_INDEX
            && !gcGames[gameIndex].available)
        {
            continue;
        }

        path =
            GC_LauncherMusicPathForGame(
                gameIndex);

        if (path == NULL)
        {
            continue;
        }

        entry =
            GC_LauncherMusicFindCache(
                path);

        if (entry == NULL)
        {
            ++failed;
            continue;
        }

        if (entry->music != NULL)
        {
            ++alreadyLoaded;
            continue;
        }

        if (GC_LauncherMusicLoadCached(
                path) != NULL)
        {
            ++loaded;
        }
        else
        {
            ++failed;
        }
    }

    DC_INFO(
        "DoomCube: launcher music preload complete "
        "loaded=%d already=%d failed=%d\n",
        loaded,
        alreadyLoaded,
        failed);
}


static bool GC_LauncherMusicUseEntry(int entryIndex)
{
    if (entryIndex == GC_OPTIONS_ENTRY_INDEX)
        return GC_LauncherMusicUseIntermission();
    return GC_LauncherMusicUseGame(entryIndex);
}

typedef struct GC_LauncherOptionsConfig
{
    gc_config_snapshot_t snapshot;
    bool available;
} GC_LauncherOptionsConfig;


static void GC_LauncherOptionsConfigInit(
    GC_LauncherOptionsConfig *config)
{
    if (config == NULL)
        return;

    GC_ConfigSnapshotInit(
        &config->snapshot);

    config->available =
        GC_ConfigSnapshotLoad(
            &config->snapshot);

    if (config->available)
    {
        DC_INFO(
            "DoomCube: OPTIONS config snapshot loaded once "
            "(%u bytes)\n",
            (unsigned int)config->snapshot.size);
    }
    else
    {
        DC_INFO(
            "DoomCube: OPTIONS config snapshot unavailable; "
            "session-only changes remain possible\n");
    }
}


static bool GC_LauncherOptionsConfigFindInt(
    const GC_LauncherOptionsConfig *config,
    const char *name,
    int *valueOut)
{
    if (name == NULL ||
        valueOut == NULL)
    {
        return false;
    }

    /*
     * A value changed earlier in this process is newer than anything on the
     * Memory Card. This also makes re-entering OPTIONS correct when the card
     * could not be written.
     */
    if (M_GetSessionVariableInt(
            (char *)name,
            valueOut))
    {
        return true;
    }

    if (config == NULL ||
        !config->available)
    {
        return false;
    }

    return
        GC_ConfigSnapshotFindInt(
            &config->snapshot,
            name,
            valueOut);
}


static bool GC_LauncherOptionsConfigSetInt(
    GC_LauncherOptionsConfig *config,
    const char *name,
    int value)
{
    if (name == NULL)
        return false;

    /*
     * Session state is independent from persistence. Install it first so the
     * game about to launch receives this value even when the Memory Card path
     * is unavailable or the subsequent write fails.
     */
    if (!M_SetSessionVariableInt(
            (char *)name,
            value))
    {
        DC_WARN(
            "DoomCube: OPTIONS could not install session override "
            "%s=%d\n",
            name,
            value);

        return false;
    }

    if (config == NULL ||
        !config->available)
    {
        DC_WARN(
            "DoomCube: OPTIONS config unavailable; "
            "%s=%d active for this session but not persisted\n",
            name,
            value);

        return false;
    }

    /*
     * Mutate the already-loaded in-RAM snapshot.  No second Memory Card read
     * occurs while OPTIONS is open.  If the write fails, the edited snapshot
     * remains in RAM so a later change can retry from the same menu-session
     * state.
     */
    if (!GC_ConfigSnapshotSetInt(
            &config->snapshot,
            name,
            value))
    {
        DC_WARN(
            "DoomCube: OPTIONS could not update config snapshot "
            "%s=%d\n",
            name,
            value);

        return false;
    }

    if (!GC_ConfigSnapshotSave(
            &config->snapshot))
    {
        DC_WARN(
            "DoomCube: OPTIONS config save failed for "
            "%s=%d; in-RAM snapshot retained\n",
            name,
            value);

        return false;
    }

    DC_INFO(
        "DoomCube: OPTIONS config persisted %s=%d "
        "from session snapshot\n",
        name,
        value);

    return true;
}


static int GC_LauncherOptionsRumbleValue(
    const GC_LauncherOptionsConfig *config)
{
    bool sessionEnabled;
    int value = 1;
    int loaded;

    /*
     * The explicit runtime override remains authoritative when present.
     * Otherwise use the single config snapshot loaded on menu entry.
     */
    if (GC_RumbleGetSessionOverride(
            &sessionEnabled))
    {
        return
            sessionEnabled ? 1 : 0;
    }

    if (GC_LauncherOptionsConfigFindInt(
            config,
            "gc_rumble_enabled",
            &loaded))
    {
        value =
            loaded ? 1 : 0;
    }

    return value;
}


static bool GC_LauncherOptionsPersistRumbleValue(
    GC_LauncherOptionsConfig *config,
    int rumbleEnabled)
{
    bool persisted =
        GC_LauncherOptionsConfigSetInt(
            config,
            "gc_rumble_enabled",
            rumbleEnabled ? 1 : 0);

    if (!persisted)
    {
        DC_WARN(
            "DoomCube: OPTIONS rumble=%d remains active "
            "for this session but was not persisted\n",
            rumbleEnabled ? 1 : 0);
    }

    return persisted;
}

static int GC_LauncherOptionsSfxVolume(
    const GC_LauncherOptionsConfig *config)
{
    int value = 8;
    int loaded;

    if (GC_LauncherOptionsConfigFindInt(
            config,
            "sfx_volume",
            &loaded))
    {
        value = loaded;
    }

    if (value < 0)
        value = 0;
    else if (value > 15)
        value = 15;

    return value;
}


static bool GC_LauncherOptionsPersistSfxVolume(
    GC_LauncherOptionsConfig *config,
    int sfxVolume)
{
    return
        GC_LauncherOptionsConfigSetInt(
            config,
            "sfx_volume",
            sfxVolume);
}


static void GC_DrawOptionsSkull(SDL_Renderer *renderer, int x, int y)
{
    SDL_Texture *texture=GC_LoadDoomMenuSkull(renderer);
    int w,h;
    SDL_Rect dst;
    if (texture == NULL) return;
    if (SDL_QueryTexture(texture,NULL,NULL,&w,&h) != 0) return;
    dst.x=x; dst.y=y; dst.w=w*2; dst.h=h*2;
    SDL_SetTextureColorMod(texture,255,255,255);
    SDL_SetTextureAlphaMod(texture,255);
    (void)SDL_RenderCopy(renderer,texture,NULL,&dst);
}

static void GC_DrawOptionsLauncher(
    SDL_Renderer *renderer,
    int selectedRow,
    int rumbleEnabled,
    int sfxVolume)
{
    SDL_Texture *background;
    SDL_Texture *bGlyph;
    SDL_Rect fullscreen =
        { 0, 0, GC_LAUNCHER_WIDTH, 480 };
    const char *rumbleValue =
        rumbleEnabled ? "ON" : "OFF";
    char sfxValue[16];
    int titleWidth;
    int valueWidth;
    int backWidth;
    int backX;
    int skullY =
        selectedRow == 0 ? 196 : 246;

    snprintf(
        sfxValue,
        sizeof(sfxValue),
        "%d",
        sfxVolume);

    SDL_SetRenderDrawColor(renderer,0,0,0,255);
    SDL_RenderClear(renderer);

    background =
        GC_LoadLauncherBitmap(
            renderer,
            GC_OPTIONS_BACKGROUND_PATH,
            "OPTIONS INTERPIC");

    if (background != NULL)
    {
        (void)SDL_RenderCopy(renderer,background,NULL,&fullscreen);
        SDL_DestroyTexture(background);
    }

    titleWidth=GC_TextWidth("OPTIONS",4);
    GC_DrawText(renderer,(GC_LAUNCHER_WIDTH-titleWidth)/2,70,"OPTIONS",4);

    GC_DrawOptionsSkull(renderer,142,skullY);

    GC_DrawText(renderer,190,210,"RUMBLE",3);
    valueWidth=GC_TextWidth(rumbleValue,3);
    GC_DrawText(renderer,450-valueWidth,210,rumbleValue,3);

    GC_DrawText(renderer,190,260,"SFX VOLUME",3);
    valueWidth=GC_TextWidth(sfxValue,3);
    GC_DrawText(renderer,450-valueWidth,260,sfxValue,3);

    bGlyph=GC_LoadCarouselControllerGlyph(renderer,&gcCarouselBGlyph,CH_CONTROLLER_GLYPH_B,GC_CAROUSEL_ACTION_GLYPH_SIZE,"OPTIONS B");
    backWidth=GC_TextWidth("BACK",3);
    backX=(GC_LAUNCHER_WIDTH-GC_CAROUSEL_ACTION_GLYPH_SIZE-8-backWidth)/2;
    if (bGlyph != NULL)
        GC_DrawCarouselControllerGlyph(renderer,bGlyph,backX,408,GC_CAROUSEL_ACTION_GLYPH_SIZE);
    GC_DrawText(renderer,backX+GC_CAROUSEL_ACTION_GLYPH_SIZE+8,421,"BACK",3);
    SDL_RenderPresent(renderer);
}

static int GC_LauncherRunOptions(SDL_Renderer *renderer)
{
    GC_LauncherOptionsConfig config;
    int selectedRow = 0;
    int rumbleEnabled;
    int sfxVolume;

    /*
     * One Memory Card config read per OPTIONS visit.
     *
     * Every row reads and edits this same in-RAM snapshot.
     */
    GC_LauncherOptionsConfigInit(
        &config);

    rumbleEnabled =
        GC_LauncherOptionsRumbleValue(
            &config);

    sfxVolume =
        GC_LauncherOptionsSfxVolume(
            &config);

    (void)GC_LauncherMusicUseIntermission();

    GC_DrawOptionsLauncher(
        renderer,
        selectedRow,
        rumbleEnabled,
        sfxVolume);

    DC_INFO(
        "DoomCube: OPTIONS opened; rumble=%d sfx_volume=%d\n",
        rumbleEnabled,
        sfxVolume);

    for (int i = 0; i < 3; ++i)
    {
        PAD_ScanPads();
        (void)PAD_ButtonsDown(0);
        SDL_Delay(16);
    }

    while (SYS_MainLoop())
    {
        u16 down;

        PAD_ScanPads();

        down =
            PAD_ButtonsDown(0);

        if (down & PAD_BUTTON_B)
        {
            DC_INFO(
                "DoomCube: OPTIONS returning to carousel\n");

            return 0;
        }

        if (down & PAD_BUTTON_UP)
        {
            if (selectedRow > 0)
                --selectedRow;

            GC_DrawOptionsLauncher(
                renderer,
                selectedRow,
                rumbleEnabled,
                sfxVolume);
        }

        if (down & PAD_BUTTON_DOWN)
        {
            if (selectedRow < 1)
                ++selectedRow;

            GC_DrawOptionsLauncher(
                renderer,
                selectedRow,
                rumbleEnabled,
                sfxVolume);
        }

        if ((down & PAD_BUTTON_A) &&
            selectedRow == 0)
        {
            bool persisted;

            rumbleEnabled =
                rumbleEnabled ? 0 : 1;

            GC_RumbleSetSessionOverride(
                rumbleEnabled != 0);

            GC_DrawOptionsLauncher(
                renderer,
                selectedRow,
                rumbleEnabled,
                sfxVolume);

            if (rumbleEnabled)
            {
                GC_RumblePulseTicks(
                    4);
            }

            persisted =
                GC_LauncherOptionsPersistRumbleValue(
                    &config,
                    rumbleEnabled);

            DC_INFO(
                "DoomCube: OPTIONS rumble changed to %d "
                "(session=1 persisted=%d)\n",
                rumbleEnabled,
                persisted ? 1 : 0);
        }

        if (selectedRow == 1 &&
            (down & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT)))
        {
            int oldVolume =
                sfxVolume;

            bool persisted;

            if ((down & PAD_BUTTON_LEFT) &&
                sfxVolume > 0)
            {
                --sfxVolume;
            }

            if ((down & PAD_BUTTON_RIGHT) &&
                sfxVolume < 15)
            {
                ++sfxVolume;
            }

            if (sfxVolume != oldVolume)
            {
                persisted =
                    GC_LauncherOptionsPersistSfxVolume(
                        &config,
                        sfxVolume);

                GC_DrawOptionsLauncher(
                    renderer,
                    selectedRow,
                    rumbleEnabled,
                    sfxVolume);

                DC_INFO(
                    "DoomCube: OPTIONS sfx_volume changed to %d "
                    "(session=1 persisted=%d)\n",
                    sfxVolume,
                    persisted ? 1 : 0);
            }
        }

        SDL_Delay(16);
    }

    return -1;
}

static Mix_Chunk *gcLauncherMenuChooseChunk;
static int gcLauncherMenuChooseChannel = -1;
static bool gcLauncherMenuChooseLoadAttempted;


static bool GC_LauncherMenuChooseEnsureLoaded(void)
{
    if (gcLauncherMenuChooseChunk != NULL)
        return true;

    if (gcLauncherMenuChooseLoadAttempted)
        return false;

    gcLauncherMenuChooseLoadAttempted = true;

    if (!GC_LauncherMusicEnsureReady())
        return false;

    gcLauncherMenuChooseChunk =
        Mix_LoadWAV(
            GC_LAUNCHER_MENU_CHOOSE_AUDIO_PATH);

    if (gcLauncherMenuChooseChunk == NULL)
    {
        DC_WARN(
            "DoomCube: launcher menu confirm sound unavailable: %s\n",
            Mix_GetError());

        return false;
    }

    DC_TRACE(
        "DoomCube: launcher menu confirm sound loaded from %s\n",
        GC_LAUNCHER_MENU_CHOOSE_AUDIO_PATH);

    return true;
}


static void GC_LauncherMenuChoosePlay(void)
{
    int channel;

    if (!GC_LauncherMenuChooseEnsureLoaded())
        return;

    if (gcLauncherMenuChooseChannel >= 0
        && Mix_Playing(gcLauncherMenuChooseChannel))
    {
        Mix_HaltChannel(
            gcLauncherMenuChooseChannel);
    }

    channel =
        Mix_PlayChannel(
            -1,
            gcLauncherMenuChooseChunk,
            0);

    if (channel < 0)
    {
        DC_WARN(
            "DoomCube: launcher menu confirm sound failed: %s\n",
            Mix_GetError());

        gcLauncherMenuChooseChannel = -1;
        return;
    }

    gcLauncherMenuChooseChannel = channel;

    DC_TRACE(
        "DoomCube: launcher DSPISTOL confirm started on mixer channel %d\n",
        channel);
}


static void GC_LauncherMenuChooseShutdown(bool waitForPlayback)
{
    if (gcLauncherMenuChooseChannel >= 0)
    {
        if (waitForPlayback)
        {
            int waits;

            for (waits = 0;
                 waits < 80
                 && Mix_Playing(gcLauncherMenuChooseChannel);
                 ++waits)
            {
                SDL_Delay(8);
            }
        }

        if (Mix_Playing(gcLauncherMenuChooseChannel))
        {
            Mix_HaltChannel(
                gcLauncherMenuChooseChannel);
        }

        gcLauncherMenuChooseChannel = -1;
    }

    if (gcLauncherMenuChooseChunk != NULL)
    {
        Mix_FreeChunk(
            gcLauncherMenuChooseChunk);

        gcLauncherMenuChooseChunk = NULL;
    }

    gcLauncherMenuChooseLoadAttempted = false;
}





static bool GC_LauncherRunSplash(
    SDL_Renderer *renderer,
    SDL_Texture *logo)
{
    SDL_Texture *collage = NULL;
    SDL_Texture *doomMenuLogo = NULL;
    SDL_Texture *splashCube = NULL;
    bool accepted = false;
    int i;

    collage =
        GC_LoadLauncherBitmap(
            renderer,
            GC_SPLASH_COLLAGE_PATH,
            "splash collage");

    doomMenuLogo =
        GC_LoadLauncherKeyedBitmap(
            renderer,
            GC_DOOM_MENU_LOGO_PATH,
            "M_DOOM",
            255,
            0,
            255);

    splashCube =
        GC_LoadLauncherKeyedBitmap(
            renderer,
            GC_SPLASH_CUBE_PATH,
            "splash DoomCube cube",
            255,
            0,
            255);

    /*
     * Consume any transition state left by startup/preflight.
     */
    for (i = 0; i < 3; ++i)
    {
        PAD_ScanPads();
        (void)PAD_ButtonsDown(0);
        SDL_Delay(16);
    }

    DC_DEBUG(
        "DoomCube: waiting at animated PRESS START splash\n");

    while (SYS_MainLoop())
    {
        u32 down;
        Uint32 phase;
        Uint8 promptAlpha;

        phase =
            SDL_GetTicks() % 1600u;

        if (phase < 800u)
        {
            promptAlpha =
                (Uint8)(
                    64u
                    + phase * 191u / 800u);
        }
        else
        {
            promptAlpha =
                (Uint8)(
                    255u
                    - (phase - 800u)
                    * 191u / 800u);
        }

        GC_DrawSplash(
            renderer,
            logo,
            collage,
            doomMenuLogo,
            splashCube,
            promptAlpha);

        PAD_ScanPads();

        down =
            PAD_ButtonsDown(0);

        if (down & PAD_BUTTON_START)
        {
            DC_DEBUG(
                "DoomCube: splash START pressed\n");

            for (i = 0; i < 3; ++i)
            {
                PAD_ScanPads();
                (void)PAD_ButtonsDown(0);
                SDL_Delay(16);
            }

            accepted = true;
            break;
        }

        SDL_Delay(16);
    }

    if (splashCube != NULL)
    {
        SDL_DestroyTexture(
            splashCube);
    }

    if (doomMenuLogo != NULL)
    {
        SDL_DestroyTexture(
            doomMenuLogo);
    }

    if (collage != NULL)
    {
        SDL_DestroyTexture(
            collage);
    }

    return accepted;
}


static int GC_LauncherRun(
    SDL_Renderer *renderer)
{
    static int selected = -1;
    int stickHeld = 0;

    GC_HideRedundantShareware();

    /*
     * Preserve the carousel position across:
     *
     *   carousel -> splash -> carousel
     *
     * and:
     *
     *   CUSTOM submenu -> carousel
     *
     * Re-resolve only if the remembered entry is no longer
     * usable.
     */
    if (selected < 0 ||
        selected >= GC_LAUNCHER_ENTRY_COUNT ||
        !GC_LauncherEntryAvailable(selected))
    {
        selected =
            GC_FirstAvailableEntry();
    }

    if (selected < 0)
        return -1;

    GC_DrawLauncher(
        renderer,
        selected);

    (void)GC_LauncherMusicUseEntry(
        selected);

    /*
     * Flush stale controller transition state before entering
     * the carousel.
     */
    for (int i = 0; i < 3; ++i)
    {
        PAD_ScanPads();
        (void)PAD_ButtonsDown(0);
        SDL_Delay(16);
    }

    /*
     * ButtonsDown() gives us button edges, but analogue stick motion
     * is level-triggered. A stick already outside the deadzone while
     * START transitions from splash -> carousel must begin in the
     * held state, not masquerade as a fresh navigation edge.
     *
     * Once the stick returns through the deadzone, the normal loop
     * below clears stickHeld and the next deliberate deflection moves
     * exactly one carousel item.
     */
    PAD_ScanPads();
    (void)PAD_ButtonsDown(0);

    {
        s8 entryStickX =
            PAD_StickX(0);

        stickHeld =
            entryStickX > GC_LAUNCHER_DEADZONE ||
            entryStickX < -GC_LAUNCHER_DEADZONE;

        DC_DEBUG(
            "DoomCube: carousel entry stick X=%d held=%d\n",
            (int)entryStickX,
            stickHeld);
    }

    DC_DEBUG(
        "DoomCube: entering horizontal carousel input loop\n");

    while (SYS_MainLoop())
    {
        u16 down;
        s8 stickX;
        int stickDirection = 0;

        PAD_ScanPads();

        down =
            PAD_ButtonsDown(0);

        stickX =
            PAD_StickX(0);

        if (stickX > GC_LAUNCHER_DEADZONE)
        {
            stickDirection = 1;
        }
        else if (stickX < -GC_LAUNCHER_DEADZONE)
        {
            stickDirection = -1;
        }

        /*
         * Previous game.
         */
        if ((down & PAD_BUTTON_LEFT) ||
            (stickDirection < 0 && !stickHeld))
        {
            int previous =
                GC_NextAvailableEntry(
                    selected,
                    -1);

            if (previous >= 0)
            {
                selected =
                    previous;

                DC_DEBUG(
                    "DoomCube: carousel previous -> %s\n",
                    GC_LauncherEntryName(selected));

                GC_DrawLauncher(
                    renderer,
                    selected);

                (void)GC_LauncherMusicUseEntry(
                    selected);
            }
        }

        /*
         * Next game.
         */
        if ((down & PAD_BUTTON_RIGHT) ||
            (stickDirection > 0 && !stickHeld))
        {
            int next =
                GC_NextAvailableEntry(
                    selected,
                    1);

            if (next >= 0)
            {
                selected =
                    next;

                DC_DEBUG(
                    "DoomCube: carousel next -> %s\n",
                    GC_LauncherEntryName(selected));

                GC_DrawLauncher(
                    renderer,
                    selected);

                (void)GC_LauncherMusicUseEntry(
                    selected);
            }
        }

        /*
         * One movement per analogue-stick deflection.
         * The player must return through the deadzone before
         * another stick movement is accepted.
         */
        stickHeld =
            stickDirection != 0;

        if (down & PAD_BUTTON_B)
        {
            DC_DEBUG(
                "DoomCube: carousel returning to splash\n");

            return -2;
        }

        if (down &
            (PAD_BUTTON_A |
             PAD_BUTTON_START))
        {
            DC_DEBUG(
                "DoomCube: carousel selected %s\n",
                GC_LauncherEntryName(selected));

            GC_LauncherMenuChoosePlay();

            return selected;
        }

        SDL_Delay(16);
    }

    return -1;
}

static const gc_game_entry_t *GC_LauncherGetGame(int index)
{
    if (index < 0 || index >= GC_MAX_GAMES)
        return NULL;

    return &gcGames[index];
}



#define GC_MEMCARD_ACTION_GLYPH_SIZE 40
#define GC_MEMCARD_ACTION_GLYPH_GAP   8
#define GC_MEMCARD_ACTION_PAIR_GAP   28
#define GC_MEMCARD_ACTION_GLYPH_Y   414
#define GC_MEMCARD_ACTION_TEXT_Y    430


static const char *GC_DoomMemCardTitle(
    CH_MemCardUIScreenKind kind)
{
    switch (kind)
    {
        case CH_MEMCARD_UI_CHECKING:
            return "CHECKING MEMORY CARD";

        case CH_MEMCARD_UI_READY:
            return "MEMORY CARD READY";

        case CH_MEMCARD_UI_NO_CARD:
            return "NO MEMORY CARD";

        case CH_MEMCARD_UI_CREATE_PROMPT:
            return "CREATE SAVE FILE";

        case CH_MEMCARD_UI_CREATED:
            return "SAVE FILE CREATED";

        case CH_MEMCARD_UI_TOO_SMALL:
            return "MEMORY CARD TOO SMALL";

        case CH_MEMCARD_UI_INSUFFICIENT_SPACE:
            return "NOT ENOUGH SPACE";

        case CH_MEMCARD_UI_DISABLED:
            return "SAVING DISABLED";

        case CH_MEMCARD_UI_ERROR:
            return "MEMORY CARD ERROR";

        default:
            return "MEMORY CARD";
    }
}


static bool GC_DoomMemCardUsesErrorArt(
    CH_MemCardUIScreenKind kind)
{
    return
        kind == CH_MEMCARD_UI_TOO_SMALL
        || kind == CH_MEMCARD_UI_INSUFFICIENT_SPACE
        || kind == CH_MEMCARD_UI_DISABLED
        || kind == CH_MEMCARD_UI_ERROR;
}


static void GC_DoomMemCardDrawCentered(
    SDL_Renderer *renderer,
    int y,
    const char *text,
    int scale)
{
    int width;

    if (
        renderer == NULL
        || text == NULL
        || text[0] == '\0'
    )
    {
        return;
    }

    width =
        GC_TextWidth(
            text,
            scale);

    GC_DrawText(
        renderer,
        (GC_LAUNCHER_WIDTH - width) / 2,
        y,
        text,
        scale);
}


static SDL_Texture *GC_DoomMemCardActionGlyph(
    SDL_Renderer *renderer,
    CH_ControllerGlyph glyph)
{
    switch (glyph)
    {
        case CH_CONTROLLER_GLYPH_A:
            return GC_LoadCarouselControllerGlyph(
                renderer,
                &gcCarouselAGlyph,
                CH_CONTROLLER_GLYPH_A,
                GC_MEMCARD_ACTION_GLYPH_SIZE,
                "memory card A");

        case CH_CONTROLLER_GLYPH_B:
            return GC_LoadCarouselControllerGlyph(
                renderer,
                &gcCarouselBGlyph,
                CH_CONTROLLER_GLYPH_B,
                GC_MEMCARD_ACTION_GLYPH_SIZE,
                "memory card B");

        default:
            return NULL;
    }
}


static int GC_DoomMemCardActionWidth(
    const CH_MemCardUIActionHint *hint)
{
    int width;

    if (hint == NULL)
        return 0;

    width =
        GC_TextWidth(
            hint->label,
            3);

    if (hint->glyph != CH_CONTROLLER_GLYPH_NONE)
    {
        width +=
            GC_MEMCARD_ACTION_GLYPH_SIZE
            + GC_MEMCARD_ACTION_GLYPH_GAP;
    }

    return width;
}


static void GC_DoomMemCardDrawActions(
    SDL_Renderer *renderer,
    const CH_MemCardUIInfo *info)
{
    unsigned int count;
    unsigned int i;
    int totalWidth = 0;
    int x;

    if (
        renderer == NULL
        || info == NULL
    )
    {
        return;
    }

    count =
        info->action_hint_count;

    if (count > CH_MEMCARD_UI_MAX_ACTION_HINTS)
        count = CH_MEMCARD_UI_MAX_ACTION_HINTS;

    if (count == 0)
        return;

    for (i = 0; i < count; ++i)
    {
        if (i != 0)
            totalWidth += GC_MEMCARD_ACTION_PAIR_GAP;

        totalWidth +=
            GC_DoomMemCardActionWidth(
                &info->action_hints[i]);
    }

    x =
        (GC_LAUNCHER_WIDTH - totalWidth) / 2;

    for (i = 0; i < count; ++i)
    {
        const CH_MemCardUIActionHint *hint =
            &info->action_hints[i];

        SDL_Texture *glyphTexture =
            GC_DoomMemCardActionGlyph(
                renderer,
                hint->glyph);

        if (i != 0)
            x += GC_MEMCARD_ACTION_PAIR_GAP;

        if (hint->glyph != CH_CONTROLLER_GLYPH_NONE)
        {
            if (glyphTexture != NULL)
            {
                GC_DrawCarouselControllerGlyph(
                    renderer,
                    glyphTexture,
                    x,
                    GC_MEMCARD_ACTION_GLYPH_Y,
                    GC_MEMCARD_ACTION_GLYPH_SIZE);
            }
            else
            {
                const char *fallback =
                    CH_ControllerGlyphName(
                        hint->glyph);

                GC_DrawText(
                    renderer,
                    x,
                    GC_MEMCARD_ACTION_TEXT_Y,
                    fallback,
                    3);
            }

            x +=
                GC_MEMCARD_ACTION_GLYPH_SIZE
                + GC_MEMCARD_ACTION_GLYPH_GAP;
        }

        GC_DrawText(
            renderer,
            x,
            GC_MEMCARD_ACTION_TEXT_Y,
            hint->label,
            3);

        x +=
            GC_TextWidth(
                hint->label,
                3);
    }
}



static bool GC_DoomMemCardRenderBackground(
    SDL_Renderer *renderer,
    SDL_Texture *texture)
{
    SDL_Rect src;
    SDL_Rect dst =
    {
        0,
        0,
        GC_LAUNCHER_WIDTH,
        480
    };

    int textureWidth;
    int textureHeight;

    if (renderer == NULL || texture == NULL)
        return false;

    if (SDL_QueryTexture(
            texture,
            NULL,
            NULL,
            &textureWidth,
            &textureHeight) != 0)
    {
        return false;
    }

    if (textureWidth <= 0 || textureHeight <= 0)
        return false;

    src.x = 0;
    src.y = 0;
    src.w = textureWidth;
    src.h = textureHeight;

    /*
     * Cover the 4:3 GameCube frame without distorting the source.
     *
     * The generated art is 320x200. Crop the longer axis around its
     * center, then scale that crop uniformly to 640x480.
     */
    if (
        (int64_t)textureWidth * 480
        > (int64_t)textureHeight * GC_LAUNCHER_WIDTH
    )
    {
        int cropWidth =
            (textureHeight * GC_LAUNCHER_WIDTH + 240)
            / 480;

        if (cropWidth < 1)
            cropWidth = 1;

        if (cropWidth > textureWidth)
            cropWidth = textureWidth;

        src.x =
            (textureWidth - cropWidth) / 2;

        src.w =
            cropWidth;
    }
    else if (
        (int64_t)textureWidth * 480
        < (int64_t)textureHeight * GC_LAUNCHER_WIDTH
    )
    {
        int cropHeight =
            (textureWidth * 480 + GC_LAUNCHER_WIDTH / 2)
            / GC_LAUNCHER_WIDTH;

        if (cropHeight < 1)
            cropHeight = 1;

        if (cropHeight > textureHeight)
            cropHeight = textureHeight;

        src.y =
            (textureHeight - cropHeight) / 2;

        src.h =
            cropHeight;
    }

    return
        SDL_RenderCopy(
            renderer,
            texture,
            &src,
            &dst) == 0;
}


static bool GC_DoomMemCardUIShow(
    void *userdata,
    CH_MemCardUIScreenKind kind,
    const CH_MemCardUIInfo *info)
{
    SDL_Renderer *renderer =
        (SDL_Renderer *)userdata;

    SDL_Texture *background = NULL;

    SDL_Rect fullscreen =
    {
        0,
        0,
        GC_LAUNCHER_WIDTH,
        480
    };

    const char *backgroundPath;
    const char *backgroundLabel;

    char line[96];
    int y = 225;

    if (
        renderer == NULL
        || info == NULL
    )
    {
        return false;
    }

    if (GC_DoomMemCardUsesErrorArt(kind))
    {
        backgroundPath =
            GC_MEMCARD_ERROR_BACKGROUND_PATH;

        backgroundLabel =
            "PFUB2 memory card background";
    }
    else
    {
        backgroundPath =
            GC_MEMCARD_NORMAL_BACKGROUND_PATH;

        backgroundLabel =
            "MWALL4_1 memory card background";
    }

    background =
        GC_LoadLauncherBitmap(
            renderer,
            backgroundPath,
            backgroundLabel);

    SDL_SetRenderDrawColor(
        renderer,
        0,
        0,
        0,
        255);

    SDL_RenderClear(
        renderer);

    if (background != NULL)
    {
        if (!GC_DoomMemCardRenderBackground(
                renderer,
                background))
        {
            DC_WARN(
                "DoomCube: memory-card background render failed: %s\n",
                SDL_GetError());
        }

        SDL_DestroyTexture(
            background);
    }

    SDL_SetRenderDrawBlendMode(
        renderer,
        SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(
        renderer,
        0,
        0,
        0,
        128);

    SDL_RenderFillRect(
        renderer,
        &fullscreen);

    SDL_SetRenderDrawBlendMode(
        renderer,
        SDL_BLENDMODE_NONE);

    SDL_SetRenderDrawColor(
        renderer,
        255,
        255,
        255,
        255);

    GC_DoomMemCardDrawCentered(
        renderer,
        52,
        GC_DoomMemCardTitle(kind),
        4);

    GC_DoomMemCardDrawCentered(
        renderer,
        145,
        info->detail,
        3);

    if (
        info->card_blocks
        != CH_MEMCARD_UI_U32_UNKNOWN
    )
    {
        snprintf(
            line,
            sizeof(line),
            "CAPACITY %u BLOCKS",
            (unsigned int)
                info->card_blocks);

        GC_DoomMemCardDrawCentered(
            renderer,
            y,
            line,
            3);

        y += 46;
    }

    if (
        info->required_blocks
        != CH_MEMCARD_UI_U32_UNKNOWN
    )
    {
        snprintf(
            line,
            sizeof(line),
            "REQUIRES %u BLOCKS",
            (unsigned int)
                info->required_blocks);

        GC_DoomMemCardDrawCentered(
            renderer,
            y,
            line,
            3);

        y += 46;
    }

    if (
        info->initial_blocks
        != CH_MEMCARD_UI_U32_UNKNOWN
    )
    {
        snprintf(
            line,
            sizeof(line),
            "STARTS AT %u BLOCKS",
            (unsigned int)
                info->initial_blocks);

        GC_DoomMemCardDrawCentered(
            renderer,
            y,
            line,
            3);

        y += 46;
    }

    if (
        info->maximum_blocks
        != CH_MEMCARD_UI_U32_UNKNOWN
    )
    {
        snprintf(
            line,
            sizeof(line),
            "CAN GROW TO %u BLOCKS",
            (unsigned int)
                info->maximum_blocks);

        GC_DoomMemCardDrawCentered(
            renderer,
            y,
            line,
            3);
    }

    GC_DoomMemCardDrawActions(
        renderer,
        info);

    SDL_RenderPresent(
        renderer);

    DC_DEBUG(
        "DoomCube: themed memory-card screen %s shown\n",
        CH_MemCardUIScreenName(kind));

    return true;
}


static CH_MemCardUI GC_DoomMemCardMakeUI(
    SDL_Renderer *renderer)
{
    CH_MemCardUI ui;

    ui.show =
        GC_DoomMemCardUIShow;

    ui.userdata =
        renderer;

    return ui;
}


typedef struct GC_StudioIdentAudio
{
    Mix_Chunk *chunk;
    int channel;
    bool openedMixer;
    bool initializedAudio;
} GC_StudioIdentAudio;


static void GC_StudioIdentAudioStop(
    GC_StudioIdentAudio *audio)
{
    if (audio == NULL)
        return;

    if (audio->channel >= 0)
    {
        Mix_HaltChannel(
            audio->channel);

        audio->channel = -1;
    }

    if (audio->chunk != NULL)
    {
        Mix_FreeChunk(
            audio->chunk);

        audio->chunk = NULL;
    }

    if (audio->openedMixer)
    {
        Mix_CloseAudio();
        audio->openedMixer = false;
    }

    if (audio->initializedAudio)
    {
        SDL_QuitSubSystem(
            SDL_INIT_AUDIO);

        audio->initializedAudio = false;
    }
}


static bool GC_StudioIdentAudioStart(
    GC_StudioIdentAudio *audio)
{
    int frequency;
    int channels;
    Uint16 format;
    int mixerRate;

    if (audio == NULL)
        return false;

    audio->chunk = NULL;
    audio->channel = -1;
    audio->openedMixer = false;
    audio->initializedAudio = false;

    if ((SDL_WasInit(SDL_INIT_AUDIO)
         & SDL_INIT_AUDIO) == 0)
    {
        if (SDL_InitSubSystem(
                SDL_INIT_AUDIO) < 0)
        {
            DC_WARN(
                "DoomCube: studio ident SDL audio init failed: %s\n",
                SDL_GetError());

            return false;
        }

        audio->initializedAudio = true;
    }

    if (!Mix_QuerySpec(
            &frequency,
            &format,
            &channels))
    {
        mixerRate =
            snd_samplerate > 0
            ? snd_samplerate
            : 44100;

        if (Mix_OpenAudio(
                mixerRate,
                AUDIO_S16SYS,
                2,
                1024) < 0)
        {
            DC_WARN(
                "DoomCube: studio ident Mix_OpenAudio failed: %s\n",
                Mix_GetError());

            GC_StudioIdentAudioStop(
                audio);

            return false;
        }

        audio->openedMixer = true;

        DC_DEBUG(
            "DoomCube: studio ident opened temporary mixer "
            "at %d Hz\n",
            mixerRate);
    }
    else
    {
        DC_DEBUG(
            "DoomCube: studio ident using existing mixer "
            "freq=%d channels=%d format=0x%x\n",
            frequency,
            channels,
            (unsigned int)format);
    }

    audio->chunk =
        Mix_LoadWAV(
            GC_STUDIO_IDENT_AUDIO_PATH);

    if (audio->chunk == NULL)
    {
        DC_WARN(
            "DoomCube: studio ident roar unavailable: %s\n",
            Mix_GetError());

        GC_StudioIdentAudioStop(
            audio);

        return false;
    }

    audio->channel =
        Mix_PlayChannel(
            -1,
            audio->chunk,
            0);

    if (audio->channel < 0)
    {
        DC_WARN(
            "DoomCube: studio ident roar playback failed: %s\n",
            Mix_GetError());

        GC_StudioIdentAudioStop(
            audio);

        return false;
    }

    DC_DEBUG(
        "DoomCube: studio ident Cyberdemon roar started "
        "on mixer channel %d\n",
        audio->channel);

    return true;
}


typedef struct GC_StudioIdentPresentation
{
    SDL_Texture *texture;
    SDL_Rect destination;
} GC_StudioIdentPresentation;


static bool GC_DrawStudioIdentFrame(
    void *userdata,
    size_t screenIndex,
    const void *screenData,
    uint8_t alpha)
{
    SDL_Renderer *renderer =
        (SDL_Renderer *)userdata;

    const GC_StudioIdentPresentation *presentation =
        (const GC_StudioIdentPresentation *)screenData;

    (void)screenIndex;

    if (renderer == NULL
        || presentation == NULL
        || presentation->texture == NULL)
    {
        return false;
    }

    SDL_SetRenderDrawColor(
        renderer,
        0,
        0,
        0,
        255);

    SDL_RenderClear(
        renderer);

    SDL_SetTextureAlphaMod(
        presentation->texture,
        (Uint8)alpha);

    SDL_RenderCopy(
        renderer,
        presentation->texture,
        NULL,
        &presentation->destination);

    SDL_RenderPresent(
        renderer);

    return true;
}


void GC_LauncherRunStudioIdent(
    SDL_Renderer *renderer)
{
    SDL_Texture *logo;
    GC_StudioIdentAudio audio;
    GC_StudioIdentPresentation presentation;
    CH_SplashScreen screens[1];
    CH_SplashSequence sequence;
    int width;
    int height;

    if (renderer == NULL)
        return;

    logo =
        GC_LoadLauncherBitmap(
            renderer,
            GC_STUDIO_IDENT_PATH,
            "Sperge Brigade Studios ident");

    if (logo == NULL)
        return;

    if (SDL_QueryTexture(
            logo,
            NULL,
            NULL,
            &width,
            &height) != 0)
    {
        DC_WARN(
            "DoomCube: studio ident texture query failed: %s\n",
            SDL_GetError());

        SDL_DestroyTexture(logo);
        return;
    }

    if (SDL_SetTextureBlendMode(
            logo,
            SDL_BLENDMODE_BLEND) != 0)
    {
        DC_WARN(
            "DoomCube: studio ident blend mode failed: %s\n",
            SDL_GetError());
    }

    presentation.texture =
        logo;

    presentation.destination.h =
        GC_STUDIO_IDENT_HEIGHT;

    presentation.destination.w =
        width * presentation.destination.h / height;

    presentation.destination.x =
        (GC_LAUNCHER_WIDTH
         - presentation.destination.w)
        / 2;

    presentation.destination.y =
        (480
         - presentation.destination.h)
        / 2;

    screens[0].screen_data =
        &presentation;

    screens[0].fade_in_ms =
        GC_STUDIO_IDENT_FADE_MS;

    screens[0].hold_ms =
        GC_STUDIO_IDENT_HOLD_MS;

    screens[0].fade_out_ms =
        GC_STUDIO_IDENT_FADE_MS;

    sequence.frame =
        GC_DrawStudioIdentFrame;

    sequence.userdata =
        renderer;

    sequence.frame_interval_ms =
        GC_STUDIO_IDENT_FRAME_MS;

    DC_DEBUG(
        "DoomCube: studio ident starting through CarryHandle "
        "(fade=%ums hold=%ums fade=%ums)\n",
        (unsigned int)screens[0].fade_in_ms,
        (unsigned int)screens[0].hold_ms,
        (unsigned int)screens[0].fade_out_ms);

    (void)GC_StudioIdentAudioStart(
        &audio);

    if (!CH_SplashSequenceRun(
            &sequence,
            screens,
            1u))
    {
        DC_WARN(
            "DoomCube: CarryHandle studio ident sequence failed\n");
    }

    GC_StudioIdentAudioStop(
        &audio);

    SDL_SetRenderDrawColor(
        renderer,
        0,
        0,
        0,
        255);

    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    SDL_DestroyTexture(logo);

    DC_DEBUG(
        "DoomCube: studio ident complete\n");
}


void GC_LauncherRunCarryHandleIdent(
    SDL_Renderer *renderer)
{
    SDL_Texture *logo;
    GC_StudioIdentPresentation presentation;
    CH_SplashScreen screens[1];
    CH_SplashSequence sequence;
    int width;
    int height;

    if (renderer == NULL)
        return;

    logo =
        GC_LoadLauncherBitmap(
            renderer,
            GC_CARRYHANDLE_IDENT_PATH,
            "Powered by CarryHandle ident");

    if (logo == NULL)
        return;

    if (SDL_QueryTexture(
            logo,
            NULL,
            NULL,
            &width,
            &height) != 0)
    {
        DC_WARN(
            "DoomCube: CarryHandle ident texture query failed: %s\n",
            SDL_GetError());

        SDL_DestroyTexture(
            logo);

        return;
    }

    if (SDL_SetTextureBlendMode(
            logo,
            SDL_BLENDMODE_BLEND) != 0)
    {
        DC_WARN(
            "DoomCube: CarryHandle ident blend mode failed: %s\n",
            SDL_GetError());
    }

    presentation.texture =
        logo;

    presentation.destination.h =
        480;

    presentation.destination.w =
        width * presentation.destination.h / height;

    presentation.destination.x =
        (GC_LAUNCHER_WIDTH
         - presentation.destination.w)
        / 2;

    presentation.destination.y =
        (480
         - presentation.destination.h)
        / 2;

    screens[0].screen_data =
        &presentation;

    screens[0].fade_in_ms =
        GC_CARRYHANDLE_IDENT_FADE_MS;

    screens[0].hold_ms =
        GC_CARRYHANDLE_IDENT_HOLD_MS;

    screens[0].fade_out_ms =
        GC_CARRYHANDLE_IDENT_FADE_MS;

    sequence.frame =
        GC_DrawStudioIdentFrame;

    sequence.userdata =
        renderer;

    sequence.frame_interval_ms =
        GC_STUDIO_IDENT_FRAME_MS;

    DC_DEBUG(
        "DoomCube: Powered by CarryHandle ident starting "
        "(fade=%ums hold=%ums fade=%ums)\n",
        (unsigned int)screens[0].fade_in_ms,
        (unsigned int)screens[0].hold_ms,
        (unsigned int)screens[0].fade_out_ms);

    if (!CH_SplashSequenceRun(
            &sequence,
            screens,
            1u))
    {
        DC_WARN(
            "DoomCube: Powered by CarryHandle ident sequence failed\n");
    }

    SDL_SetRenderDrawColor(
        renderer,
        0,
        0,
        0,
        255);

    SDL_RenderClear(
        renderer);

    SDL_RenderPresent(
        renderer);

    SDL_DestroyTexture(
        logo);

    DC_DEBUG(
        "DoomCube: Powered by CarryHandle ident complete\n");
}


void GC_LauncherShowMemoryCardChecking(
    SDL_Renderer *renderer)
{
    CH_MemCardUI ui;
    CH_MemCardUIInfo info;

    if (renderer == NULL)
        return;

    ui =
        GC_DoomMemCardMakeUI(
            renderer);

    CH_MemCardUIInfoInit(
        &info);

    info.slot = 0;
    info.application_name = "DOOMCUBE";

    (void)CH_MemCardUIShow(
        &ui,
        CH_MEMCARD_UI_CHECKING,
        &info);
}


static void GC_LauncherShowSaveFileCreated(
    SDL_Renderer *renderer)
{
    CH_MemCardUI ui;
    CH_MemCardUIInfo info;
    int i;

    if (renderer == NULL)
        return;

    ui =
        GC_DoomMemCardMakeUI(
            renderer);

    CH_MemCardUIInfoInit(
        &info);

    info.slot = 0;
    info.application_name = "DOOMCUBE";
    info.detail =
        "DOOMCUBE SAVING IS ENABLED";

    info.initial_blocks =
        GC_MemoryCardSaveFileInitialBlocks();

    info.action_hint_count = 1;
    info.action_hints[0].glyph =
        CH_CONTROLLER_GLYPH_A;
    info.action_hints[0].label =
        "CONTINUE";

    (void)CH_MemCardUIShow(
        &ui,
        CH_MEMCARD_UI_CREATED,
        &info);

    SYS_Report(
        "DoomCube: themed save-file-created screen shown: "
        "initial=%u blocks\n",
        (unsigned int)
            GC_MemoryCardSaveFileInitialBlocks());

    for (i = 0; i < 3; ++i)
    {
        PAD_ScanPads();
        (void)PAD_ButtonsDown(PAD_CHAN0);
        SDL_Delay(16);
    }

    while (SYS_MainLoop())
    {
        u16 down;

        PAD_ScanPads();

        down =
            PAD_ButtonsDown(
                PAD_CHAN0);

        if (
            down
            & (
                PAD_BUTTON_A
                | PAD_BUTTON_START
            )
        )
        {
            break;
        }

        SDL_Delay(16);
    }

    SYS_Report(
        "DoomCube: themed save-file-created screen dismissed\n");
}


void GC_LauncherRunStoragePreflight(
    SDL_Renderer *renderer)
{
    gc_memcard_status_t status;
    CH_MemCardUI ui;
    CH_MemCardUIInfo info;
    int i;

    if (renderer == NULL)
        return;

    status =
        GC_MemoryCardGetStatus();

    if (
        status != GC_MEMCARD_STATUS_TOO_SMALL
        && status != GC_MEMCARD_STATUS_NEEDS_CREATE
    )
    {
        return;
    }

    ui =
        GC_DoomMemCardMakeUI(
            renderer);

    if (status == GC_MEMCARD_STATUS_TOO_SMALL)
    {
        CH_MemCardUIInfoInit(
            &info);

        info.slot = 0;
        info.application_name = "DOOMCUBE";
        info.detail =
            "MEMORY CARD 251 OR LARGER REQUIRED";
        info.card_blocks = 59;

        info.action_hint_count = 1;
        info.action_hints[0].glyph =
            CH_CONTROLLER_GLYPH_A;
        info.action_hints[0].label =
            "CONTINUE";

        (void)CH_MemCardUIShow(
            &ui,
            CH_MEMCARD_UI_TOO_SMALL,
            &info);

        SYS_Report(
            "DoomCube: themed Memory Card 59 warning shown\n");

        for (i = 0; i < 3; ++i)
        {
            PAD_ScanPads();
            (void)PAD_ButtonsDown(PAD_CHAN0);
            SDL_Delay(16);
        }

        while (SYS_MainLoop())
        {
            u16 down;

            PAD_ScanPads();

            down =
                PAD_ButtonsDown(
                    PAD_CHAN0);

            if (
                down
                & (
                    PAD_BUTTON_A
                    | PAD_BUTTON_START
                )
            )
            {
                break;
            }

            SDL_Delay(16);
        }

        SYS_Report(
            "DoomCube: themed Memory Card 59 warning dismissed\n");

        return;
    }

    if (status == GC_MEMCARD_STATUS_NEEDS_CREATE)
    {
        CH_MemCardUIInfoInit(
            &info);

        info.slot = 0;
        info.application_name = "DOOMCUBE";
        info.detail =
            "NO DOOMCUBE SAVE FILE FOUND";

        info.initial_blocks =
            GC_MemoryCardSaveFileInitialBlocks();

        info.maximum_blocks =
            GC_MemoryCardSaveFileMaxBlocks();

        info.action_hint_count = 2;

        info.action_hints[0].glyph =
            CH_CONTROLLER_GLYPH_A;
        info.action_hints[0].label =
            "CREATE";

        info.action_hints[1].glyph =
            CH_CONTROLLER_GLYPH_B;
        info.action_hints[1].label =
            "WITHOUT SAVING";

        (void)CH_MemCardUIShow(
            &ui,
            CH_MEMCARD_UI_CREATE_PROMPT,
            &info);

        SYS_Report(
            "DoomCube: themed save-file creation prompt shown: "
            "initial=%u maximum=%u blocks\n",
            (unsigned int)
                GC_MemoryCardSaveFileInitialBlocks(),
            (unsigned int)
                GC_MemoryCardSaveFileMaxBlocks());

        for (i = 0; i < 3; ++i)
        {
            PAD_ScanPads();
            (void)PAD_ButtonsDown(PAD_CHAN0);
            SDL_Delay(16);
        }

        while (SYS_MainLoop())
        {
            u16 down;

            PAD_ScanPads();

            down =
                PAD_ButtonsDown(
                    PAD_CHAN0);

            if (down & PAD_BUTTON_A)
            {
                SYS_Report(
                    "DoomCube: player selected CREATE save file\n");

                if (GC_MemoryCardCreateSaveFile())
                {
                    SYS_Report(
                        "DoomCube: pre-launch DoomCube save-file "
                        "creation succeeded\n");

                    GC_LauncherShowSaveFileCreated(
                        renderer);

                    return;
                }

                SYS_Report(
                    "DoomCube: pre-launch DoomCube save-file "
                    "creation failed; saving disabled\n");

                GC_MemoryCardShutdown();

                return;
            }

            if (down & PAD_BUTTON_B)
            {
                SYS_Report(
                    "DoomCube: player declined DoomCube save-file "
                    "creation; saving disabled for this session\n");

                GC_MemoryCardShutdown();

                return;
            }

            SDL_Delay(16);
        }
    }
}





bool GC_LauncherSelectGame(
    SDL_Renderer *renderer,
    gc_launch_selection_t *selection)
{
    int availableGames;
    int selectedGame;
    const gc_game_entry_t *game;
    SDL_Texture *logo;

    if (selection == NULL)
    {
        DC_ERROR(
            "DoomCube: NULL launcher selection output\n");

        return false;
    }

    selection->iwadPath = NULL;
    selection->pwadPath = NULL;

    /*
     * Observe the WAD-independent global configuration before Doom starts.
     * Doom will bind and apply this same payload later through its normal
     * configuration lifecycle.
     */
    GC_LauncherProbeGlobalConfig();

    GC_LauncherScanPwads();

    availableGames =
        GC_LauncherScanGames();

#ifdef DOOMCUBE_REGRESSION
    /*
     * Automated regression builds select their IWAD/PWAD at runtime.
     *
     * The test case itself comes from gc_regression.c, but the paths are
     * still resolved against the launcher's normal scan results so the
     * regression suite exercises the real availability checks.
     */
    {
        const gc_regression_case_t *test;
        int i;

        test =
            GC_RegressionGetCase();

        if (test == NULL)
        {
            DC_ERROR(
                "DoomCube: REGRESSION has no selected case\n");

            return false;
        }

        game = NULL;

        for (i = 0; i < GC_MAX_GAMES; ++i)
        {
            if (gcGames[i].available
             && strcmp(
                    gcGames[i].iwadPath,
                    test->iwadPath) == 0)
            {
                game = &gcGames[i];
                break;
            }
        }

        if (game == NULL)
        {
            DC_ERROR(
                "DoomCube: REGRESSION IWAD not found: %s\n",
                test->iwadPath);

            return false;
        }

        selection->iwadPath =
            game->iwadPath;

        selection->pwadPath = NULL;

        if (test->pwadPath != NULL)
        {
            for (i = 0; i < gcAvailablePwadCount; ++i)
            {
                if (strcmp(
                        gcPwads[i].path,
                        test->pwadPath) == 0)
                {
                    selection->pwadPath =
                        gcPwads[i].path;

                    break;
                }
            }

            if (selection->pwadPath == NULL)
            {
                DC_ERROR(
                    "DoomCube: REGRESSION PWAD not found: %s\n",
                    test->pwadPath);

                return false;
            }
        }

        GC_MemoryCardSetGame(
            game->saveGameId);

        DC_INFO(
            "DoomCube: REGRESSION AUTOSELECT IWAD: %s\n",
            selection->iwadPath);

        if (selection->pwadPath != NULL)
        {
            DC_INFO(
                "DoomCube: REGRESSION AUTOSELECT PWAD: %s\n",
                selection->pwadPath);
        }

        return true;
    }
#endif

#if defined(DOOMCUBE_TEST_IWAD_PATH)
    /*
     * Developer-only launcher bypass for automated regression tests.
     *
     * Resolve the requested paths against the launcher's normal scan
     * results so tests exercise the same availability checks and game
     * metadata as an interactive launch.
     */
    {
        int i;

        game = NULL;

        for (i = 0; i < GC_MAX_GAMES; ++i)
        {
            if (gcGames[i].available
             && strcmp(
                    gcGames[i].iwadPath,
                    DOOMCUBE_TEST_IWAD_PATH) == 0)
            {
                game = &gcGames[i];
                break;
            }
        }

        if (game == NULL)
        {
            DC_ERROR(
                "DoomCube: TEST AUTOSELECT IWAD not found: %s\n",
                DOOMCUBE_TEST_IWAD_PATH);

            return false;
        }

        selection->iwadPath = game->iwadPath;
        selection->pwadPath = NULL;

#if defined(DOOMCUBE_TEST_PWAD_PATH)
        for (i = 0; i < gcAvailablePwadCount; ++i)
        {
            if (strcmp(
                    gcPwads[i].path,
                    DOOMCUBE_TEST_PWAD_PATH) == 0)
            {
                selection->pwadPath = gcPwads[i].path;
                break;
            }
        }

        if (selection->pwadPath == NULL)
        {
            DC_ERROR(
                "DoomCube: TEST AUTOSELECT PWAD not found: %s\n",
                DOOMCUBE_TEST_PWAD_PATH);

            return false;
        }
#endif

        GC_MemoryCardSetGame(
            game->saveGameId);

        DC_INFO(
            "DoomCube: TEST AUTOSELECT IWAD: %s\n",
            selection->iwadPath);

        if (selection->pwadPath != NULL)
        {
            DC_INFO(
                "DoomCube: TEST AUTOSELECT PWAD: %s\n",
                selection->pwadPath);
        }

        return true;
    }
#endif

    if (availableGames == 0)
    {
        DC_WARN(
            "DoomCube: no supported IWADs found on disc\n");

        return false;
    }

    logo =
        GC_LoadLauncherLogo(renderer);

    /*
     * Populate the launcher MIDI cache synchronously behind an explicit
     * loading screen BEFORE exposing PRESS START.
     */
    SDL_SetRenderDrawColor(
        renderer,
        0,
        0,
        0,
        255);

    SDL_RenderClear(
        renderer);

    GC_DrawLoadingOverlay(
        renderer);

    GC_LauncherMusicPreloadAvailable();

    /*
     * custom.mid is cached now, so title/intermission music starts
     * without another Mix_LoadMUS stall.
     */
    (void)GC_LauncherMusicUseIntermission();

    if (!GC_LauncherRunSplash(
            renderer,
            logo))
    {
        if (logo != NULL)
        {
            SDL_DestroyTexture(logo);

        GC_DoomMenuSkullShutdown();
        GC_LauncherControllerGlyphShutdown();
        GC_DoomFontShutdown();
        }

        GC_LauncherMusicShutdown();

        return false;
    }

    for (;;)
    {
        selectedGame =
            GC_LauncherRun(
                renderer);

        if (selectedGame == -2)
        {
            /*
             * B from the game-selection screen returns to the
             * DoomCube root splash.  START re-enters selection.
             */
            (void)GC_LauncherMusicUseIntermission();

            if (!GC_LauncherRunSplash(
                    renderer,
                    logo))
            {
                break;
            }

            continue;
        }

        if (selectedGame < 0)
        {
            break;
        }

        if (selectedGame == GC_OPTIONS_ENTRY_INDEX)
        {
            (void)GC_LauncherRunOptions(renderer);
            continue;
        }

        game =
            GC_LauncherGetGame(
                selectedGame);

        if (!game)
        {
            DC_WARN(
                "DoomCube: invalid launcher selection\n");

            break;
        }

        if (selectedGame == GC_CUSTOM_GAME_INDEX)
        {
            for (;;)
            {
                int selectedBase =
                    GC_LauncherRunCustomBase(
                        renderer);

                /*
                 * B from SELECT BASE returns to the carousel.
                 */
                if (selectedBase == -2)
                {
                    break;
                }

                if (selectedBase < 0)
                {
                    DC_WARN(
                        "DoomCube: CUSTOM has no selectable base\n");

                    break;
                }

                {
                    int selectedPwad =
                        GC_LauncherRunCustomPwad(
                            renderer,
                            selectedBase);

                    /*
                     * B from SELECT PWAD returns one level to
                     * SELECT BASE.
                     */
                    if (selectedPwad == -2)
                    {
                        continue;
                    }

                    if (selectedPwad < 0)
                    {
                        DC_WARN(
                            "DoomCube: CUSTOM base %s has no selectable PWAD\n",
                            gcCustomBases[selectedBase].name);

                        continue;
                    }

                    selection->iwadPath =
                        gcCustomBases[selectedBase].iwadPath;

                    selection->pwadPath =
                        gcPwads[selectedPwad].path;

                    GC_LauncherMusicShutdown();

                    GC_DrawLoadingOverlay(
                        renderer);

                    GC_MemoryCardSetGame(
                        gcCustomBases[
                            selectedBase
                        ].saveGameId);

                    DC_INFO(
                        "DoomCube: CUSTOM launcher selected base %s (%s)\n",
                        gcCustomBases[selectedBase].name,
                        selection->iwadPath);

                    DC_INFO(
                        "DoomCube: CUSTOM launcher add-on %s\n",
                        selection->pwadPath);

                    if (logo != NULL)
                    {
                        SDL_DestroyTexture(
                            logo);

                    GC_DoomMenuSkullShutdown();
                    GC_LauncherControllerGlyphShutdown();
                    GC_DoomFontShutdown();
                    }

                    return true;
                }
            }

            /*
             * Leaving SELECT BASE with B returns to the
             * already-selected CUSTOM carousel entry.
             */
            continue;
        }


        selection->iwadPath =
            game->iwadPath;
        selection->pwadPath =
            game->pwadPath;


        GC_LauncherMusicShutdown();

        GC_DrawLoadingOverlay(renderer);

        GC_MemoryCardSetGame(
            game->saveGameId);

        DC_INFO(
            "DoomCube: launcher selected %s (%s)\n",
            game->name,
            game->iwadPath);

        if (selection->pwadPath != NULL)
        {
            DC_INFO(
                "DoomCube: launcher add-on %s\n",
                selection->pwadPath);
        }

        if (logo != NULL)
        {
            SDL_DestroyTexture(logo);

        GC_DoomMenuSkullShutdown();
        GC_LauncherControllerGlyphShutdown();
        GC_DoomFontShutdown();
        }

        return true;
    }

    if (logo != NULL)
    {
        SDL_DestroyTexture(logo);

        GC_DoomMenuSkullShutdown();
        GC_LauncherControllerGlyphShutdown();
        GC_DoomFontShutdown();
    }

    GC_LauncherMusicShutdown();

    return false;
}
