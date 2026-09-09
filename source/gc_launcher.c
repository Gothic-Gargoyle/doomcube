/* ------------------------------------------------------------------------- */
/* DoomCube launcher                                                         */
/* ------------------------------------------------------------------------- */

#include "gc_debug.h"

#include "gc_launcher.h"
#include "gc_memcard.h"
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

#define GC_LAUNCHER_LOGO_PATH    "dvd:/launcher/doomcube.bmp"
#define GC_LAUNCHER_LOGO_Y       5

#define GC_MAX_GAMES 8
#define GC_CUSTOM_GAME_INDEX 7

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
    { "DOOM SHAREWARE", "dvd:/data/wad/doom1.wad",    NULL,                                             NULL, false, GC_SAVEGAME_DOOM1 },
    { "DOOM",           "dvd:/data/wad/doom.wad",     NULL,                                             NULL, false, GC_SAVEGAME_DOOM },
    { "DOOM II",        "dvd:/data/wad/doom2.wad",    NULL,                                             NULL, false, GC_SAVEGAME_DOOM2 },
    { "TNT: EVILUTION", "dvd:/data/wad/tnt.wad",      NULL,                                             NULL, false, GC_SAVEGAME_TNT },
    { "PLUTONIA",       "dvd:/data/wad/plutonia.wad", NULL,                                             NULL, false, GC_SAVEGAME_PLUTONIA },
    { "SIGIL",          "dvd:/data/wad/doom.wad",     "dvd:/data/pwad/doom/SIGIL_V1_23.wad",           NULL, false, GC_SAVEGAME_DOOM },
    { "SIGIL II",       "dvd:/data/wad/doom.wad",     "dvd:/data/pwad/doom/SIGIL_II_V1_0.WAD",         NULL, false, GC_SAVEGAME_DOOM },
    { "CUSTOM",         NULL,                           NULL,                                             NULL, false, GC_SAVEGAME_DOOM }
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

static SDL_Texture *GC_LoadLauncherLogo(SDL_Renderer *renderer)
{
    SDL_Surface *loaded;
    SDL_Surface *converted;
    SDL_Texture *texture;

    loaded = SDL_LoadBMP(GC_LAUNCHER_LOGO_PATH);

    if (loaded == NULL)
    {
        DC_WARN(
            "DoomCube: launcher logo load failed: %s\n",
            SDL_GetError());

        return NULL;
    }

    DC_DEBUG(
        "DoomCube: launcher BMP: %dx%d, format=%s, pitch=%d\n",
        loaded->w,
        loaded->h,
        SDL_GetPixelFormatName(loaded->format->format),
        loaded->pitch);

    /*
     * Do not hand SDL_CreateTextureFromSurface() whatever native
     * pixel format SDL_LoadBMP() happened to produce.
     *
     * Convert explicitly to 32-bit RGBA first.  This avoids the
     * GameCube renderer having to deal with the BMP's native BGR
     * surface format.
     */
    converted = SDL_ConvertSurfaceFormat(
        loaded,
        SDL_PIXELFORMAT_RGBA32,
        0);

    SDL_FreeSurface(loaded);

    if (converted == NULL)
    {
        DC_WARN(
            "DoomCube: launcher logo conversion failed: %s\n",
            SDL_GetError());

        return NULL;
    }

    DC_DEBUG(
        "DoomCube: converted logo: %dx%d, format=%s, pitch=%d\n",
        converted->w,
        converted->h,
        SDL_GetPixelFormatName(converted->format->format),
        converted->pitch);

    texture =
        SDL_CreateTextureFromSurface(
            renderer,
            converted);

    SDL_FreeSurface(converted);

    if (texture == NULL)
    {
        DC_WARN(
            "DoomCube: launcher logo texture creation failed: %s\n",
            SDL_GetError());

        return NULL;
    }

    DC_DEBUG(
        "DoomCube: launcher logo loaded from %s\n",
        GC_LAUNCHER_LOGO_PATH);

    return texture;
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

static const uint8_t *GC_FontGlyph(char c)
{
    static const uint8_t blank[7] = { 0, 0, 0, 0, 0, 0, 0 };

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

    static const uint8_t colon[7]  = { 0, 4, 4, 0, 4, 4, 0 };
    static const uint8_t dash[7]   = { 0, 0, 0, 31, 0, 0, 0 };
    static const uint8_t period[7] = { 0, 0, 0, 0, 0, 4, 4 };
    static const uint8_t lparen[7] = { 2, 4, 8, 8, 8, 4, 2 };
    static const uint8_t rparen[7] = { 8, 4, 2, 2, 2, 4, 8 };

    static const uint8_t digits[10][7] =
{
    {14,17,19,21,25,17,14}, /* 0 */
    {4,12,4,4,4,4,14},      /* 1 */
    {14,17,1,2,4,8,31},     /* 2 */
    {30,1,1,14,1,1,30},     /* 3 */
    {2,6,10,18,31,2,2},     /* 4 */
    {31,16,16,30,1,1,30},   /* 5 */
    {14,16,16,30,17,17,14}, /* 6 */
    {31,1,2,4,8,8,8},       /* 7 */
    {14,17,17,14,17,17,14}, /* 8 */
    {14,17,17,15,1,1,14}    /* 9 */
};

static const uint8_t copyleft[7] =
{
    14, /* 01110 */
    17, /* 10001 */
    13, /* 10110 */
    9, /* 10010 */
    13, /* 10110 */
    17, /* 10001 */
    14  /* 01110 */
};

    unsigned char uc = (unsigned char)toupper((unsigned char)c);

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

static void GC_DrawChar(
    SDL_Renderer *renderer, int x, int y, char c, int scale)
{
    const uint8_t *glyph = GC_FontGlyph(c);
    SDL_Rect pixel = { 0, 0, scale, scale };
    int row;
    int col;

    for (row = 0; row < 7; ++row)
    {
        for (col = 0; col < 5; ++col)
        {
            if ((glyph[row] & (1u << (4 - col))) == 0)
                continue;

            pixel.x = x + col * scale;
            pixel.y = y + row * scale;

            SDL_RenderFillRect(renderer, &pixel);
        }
    }
}

static void GC_DrawText(
    SDL_Renderer *renderer,
    int x,
    int y,
    const char *text,
    int scale)
{
    size_t i;
    size_t length;

    if (!text)
        return;

    length = strlen(text);

    for (i = 0; i < length; ++i)
    {
        GC_DrawChar(
            renderer,
            x + (int)i * 6 * scale,
            y,
            text[i],
            scale);
    }
}

static int GC_TextWidth(const char *text, int scale)
{
    if (!text)
        return 0;

    return (int)strlen(text) * 6 * scale;
}

static int GC_FirstAvailableGame(void)
{
    int i;

    for (i = 0; i < GC_MAX_GAMES; ++i)
    {
        if (gcGames[i].available)
            return i;
    }

    return -1;
}

static int GC_NextAvailableGame(int current, int direction)
{
    int attempts;

    for (attempts = 0; attempts < GC_MAX_GAMES; ++attempts)
    {
        current += direction;

        if (current < 0)
            current = GC_MAX_GAMES - 1;
        else if (current >= GC_MAX_GAMES)
            current = 0;

        if (gcGames[current].available)
            return current;
    }

    return -1;
}

static void GC_DrawLauncher(
    SDL_Renderer *renderer,
    int selected)
{
    int previous;
    int next;
    int textWidth;

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

    /*
     * Temporary pre-artwork carousel presentation.
     *
     * The full-screen TITLEPIC will replace this empty upper
     * area later.  Keep the actual carousel geometry simple
     * and stable so navigation can be proven independently.
     */
    {
        const char *heading = "SELECT GAME";

        GC_DrawText(
            renderer,
            (GC_LAUNCHER_WIDTH -
                GC_TextWidth(heading, 3)) / 2,
            75,
            heading,
            3);
    }

    /*
     * Selected game.
     */
    {
        SDL_Rect marker =
        {
            80,
            170,
            480,
            72
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

        textWidth =
            GC_TextWidth(
                gcGames[selected].name,
                4);

        GC_DrawText(
            renderer,
            (GC_LAUNCHER_WIDTH - textWidth) / 2,
            192,
            gcGames[selected].name,
            4);
    }

    previous =
        GC_NextAvailableGame(
            selected,
            -1);

    next =
        GC_NextAvailableGame(
            selected,
            1);

    /*
     * Previous item.
     *
     * Do not repeat the selected game if this disc only has
     * one available carousel entry.
     */
    if (previous >= 0 &&
        previous != selected)
    {
        int width =
            GC_TextWidth(
                gcGames[previous].name,
                2);

        GC_DrawText(
            renderer,
            160 - width / 2,
            305,
            gcGames[previous].name,
            2);
    }

    /*
     * Next item.
     */
    if (next >= 0 &&
        next != selected)
    {
        int width =
            GC_TextWidth(
                gcGames[next].name,
                2);

        GC_DrawText(
            renderer,
            480 - width / 2,
            305,
            gcGames[next].name,
            2);
    }

    /*
     * Temporary navigation hints.
     *
     * These will eventually be rendered with the extracted
     * Doom font over the TITLEPIC safe area.
     */
    {
        const char *selectText =
            "LEFT RIGHT - SELECT";

        const char *actionText =
            "A - START    B - BACK";

        GC_DrawText(
            renderer,
            (GC_LAUNCHER_WIDTH -
                GC_TextWidth(selectText, 2)) / 2,
            365,
            selectText,
            2);

        GC_DrawText(
            renderer,
            (GC_LAUNCHER_WIDTH -
                GC_TextWidth(actionText, 2)) / 2,
            400,
            actionText,
            2);
    }

    SDL_RenderPresent(renderer);
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

        if (i == selected)
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

        textWidth =
            GC_TextWidth(
                gcCustomBases[i].name,
                2);

        GC_DrawText(
            renderer,
            (GC_LAUNCHER_WIDTH -
                textWidth) / 2,
            y,
            gcCustomBases[i].name,
            2);

        ++shown;
    }

    GC_DrawText(
        renderer,
        (GC_LAUNCHER_WIDTH -
            GC_TextWidth(controls, 2)) / 2,
        400,
        controls,
        2);

    SDL_RenderPresent(renderer);
}

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

            return selected;
        }

        SDL_Delay(16);
    }
}

static void GC_DrawLoadingOverlay(SDL_Renderer *renderer)
{
    const char *text = "LOADING";
    int textWidth;
    SDL_Rect box =
    {
        190,
        205,
        260,
        70
    };

    /*
     * Draw an opaque box over the existing launcher screen.
     * Avoid alpha/blending tricks here so the GameCube SDL renderer
     * has as little work to do as possible.
     */
    SDL_SetRenderDrawColor(
        renderer,
        255,
        155,
        0,
        255);

    SDL_RenderFillRect(
        renderer,
        &box);

    SDL_SetRenderDrawColor(
        renderer,
        255,
        255,
        255,
        255);

    textWidth =
        GC_TextWidth(
            text,
            GC_LAUNCHER_FONT_SCALE);

    GC_DrawText(
        renderer,
        (GC_LAUNCHER_WIDTH - textWidth) / 2,
        228,
        text,
        GC_LAUNCHER_FONT_SCALE);

    SDL_RenderPresent(renderer);
}

static void GC_DrawSplash(
    SDL_Renderer *renderer,
    SDL_Texture *logo)
{
    const char *prompt = "PRESS START";
    const char *copyleft =
        "@ COPYLEFT 2026 SPERGE BRIGADE STUDIOS";
    char versionText[96];
    int versionWidth;

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

    if (logo != NULL)
    {
        int logoWidth;
        int logoHeight;

        if (SDL_QueryTexture(
                logo,
                NULL,
                NULL,
                &logoWidth,
                &logoHeight) == 0)
        {
            SDL_Rect logoRect;

            logoRect.x =
                (GC_LAUNCHER_WIDTH - logoWidth) / 2;

            /*
             * Move the logo slightly above true vertical centre
             * so PRESS START has comfortable breathing room.
             */
            logoRect.y =
                (480 - logoHeight) / 2 - 35;

            if (logoRect.y < 20)
            {
                logoRect.y = 20;
            }

            logoRect.w = logoWidth;
            logoRect.h = logoHeight;

            SDL_RenderCopy(
                renderer,
                logo,
                NULL,
                &logoRect);
        }
        else
        {
            DC_WARN(
                "DoomCube: splash SDL_QueryTexture failed: %s\n",
                SDL_GetError());
        }
    }
    else
    {
        const char *title = "DOOMCUBE";
        int titleWidth =
            GC_TextWidth(
                title,
                5);

        GC_DrawText(
            renderer,
            (GC_LAUNCHER_WIDTH - titleWidth) / 2,
            145,
            title,
            5);
    }

    GC_DrawText(
        renderer,
        (GC_LAUNCHER_WIDTH -
            GC_TextWidth(prompt, 3)) / 2,
        350,
        prompt,
        3);

    /*
     * Splash footer.
     *
     * Keep the project credit and build identification in their
     * own fixed region so neither depends on the logo dimensions.
     */
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


static bool GC_LauncherRunSplash(
    SDL_Renderer *renderer,
    SDL_Texture *logo)
{
    int i;

    GC_DrawSplash(
        renderer,
        logo);

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
        "DoomCube: waiting at PRESS START splash\n");

    while (SYS_MainLoop())
    {
        u32 down;

        PAD_ScanPads();

        down =
            PAD_ButtonsDown(0);

        /*
         * This is deliberately START-only.
         *
         * A belongs to game selection after the splash;
         * B belongs to carousel -> splash navigation.
         */
        if (down & PAD_BUTTON_START)
        {
            DC_DEBUG(
                "DoomCube: splash START pressed\n");

            /*
             * Explicitly consume the splash transition.
             *
             * GC_LauncherRun() also has its own stale-input
             * flush, so the menu is protected on both sides
             * against START immediately launching a game.
             */
            for (i = 0; i < 3; ++i)
            {
                PAD_ScanPads();
                (void)PAD_ButtonsDown(0);
                SDL_Delay(16);
            }

            return true;
        }

        SDL_Delay(16);
    }

    return false;
}


static int GC_LauncherRun(
    SDL_Renderer *renderer)
{
    static int selected = -1;
    int stickHeld = 0;

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
        selected >= GC_MAX_GAMES ||
        !gcGames[selected].available)
    {
        selected =
            GC_FirstAvailableGame();
    }

    if (selected < 0)
        return -1;

    GC_DrawLauncher(
        renderer,
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
                GC_NextAvailableGame(
                    selected,
                    -1);

            if (previous >= 0)
            {
                selected =
                    previous;

                DC_DEBUG(
                    "DoomCube: carousel previous -> %s\n",
                    gcGames[selected].name);

                GC_DrawLauncher(
                    renderer,
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
                GC_NextAvailableGame(
                    selected,
                    1);

            if (next >= 0)
            {
                selected =
                    next;

                DC_DEBUG(
                    "DoomCube: carousel next -> %s\n",
                    gcGames[selected].name);

                GC_DrawLauncher(
                    renderer,
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
                gcGames[selected].name);

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


static void GC_LauncherShowSaveFileCreated(
    SDL_Renderer *renderer)
{
    static const char *line1 =
        "SAVE FILE CREATED";

    static const char *line2 =
        "DOOMCUBE SAVING IS ENABLED";

    static const char *line4 =
        "PRESS A OR START TO CONTINUE";

    char sizeLine[64];

    SDL_Rect panel;

    int width = 640;
    int height = 480;

    int textWidth;

    int i;

    if (!renderer)
    {
        return;
    }

    snprintf(
        sizeLine,
        sizeof(sizeLine),
        "INITIAL SIZE %u BLOCKS",
        (unsigned int)
            GC_MemoryCardSaveFileInitialBlocks()
    );

    if (SDL_GetRendererOutputSize(
            renderer,
            &width,
            &height) != 0)
    {
        width = 640;
        height = 480;
    }

    /*
     * Make successful creation visually impossible to confuse
     * with the purple question/warning screens.
     */
    SDL_SetRenderDrawColor(
        renderer,
        0,
        0,
        0,
        255
    );

    SDL_RenderClear(
        renderer
    );

    panel.x = 35;
    panel.y = 55;
    panel.w = width - 70;
    panel.h = height - 110;

    /*
     * Strong orange confirmation panel.
     */
    SDL_SetRenderDrawColor(
        renderer,
        235,
        120,
        20,
        255
    );

    SDL_RenderFillRect(
        renderer,
        &panel
    );

    /*
     * Black text gives strong contrast against orange.
     */
    SDL_SetRenderDrawColor(
        renderer,
        0,
        0,
        0,
        255
    );

    textWidth =
        GC_TextWidth(
            line1,
            3
        );

    GC_DrawText(
        renderer,
        (width - textWidth) / 2,
        115,
        line1,
        3
    );

    textWidth =
        GC_TextWidth(
            line2,
            2
        );

    GC_DrawText(
        renderer,
        (width - textWidth) / 2,
        200,
        line2,
        2
    );

    textWidth =
        GC_TextWidth(
            sizeLine,
            2
        );

    GC_DrawText(
        renderer,
        (width - textWidth) / 2,
        250,
        sizeLine,
        2
    );

    textWidth =
        GC_TextWidth(
            line4,
            2
        );

    GC_DrawText(
        renderer,
        (width - textWidth) / 2,
        345,
        line4,
        2
    );

    SDL_RenderPresent(
        renderer
    );

    SYS_Report(
        "DoomCube: orange save-file-created screen shown: "
        "initial=%u blocks\n",
        (unsigned int)
            GC_MemoryCardSaveFileInitialBlocks()
    );

    /*
     * Consume transition state from the CREATE press.
     *
     * The orange screen should require a deliberate second
     * A/START press rather than disappearing instantly.
     */
    for (i = 0;
         i < 3;
         ++i)
    {
        PAD_ScanPads();

        (void)PAD_ButtonsDown(
            PAD_CHAN0
        );

        SDL_Delay(
            16
        );
    }

    while (SYS_MainLoop())
    {
        u16 down;

        PAD_ScanPads();

        down =
            PAD_ButtonsDown(
                PAD_CHAN0
            );

        if (down &
            (
                PAD_BUTTON_A |
                PAD_BUTTON_START
            ))
        {
            break;
        }

        SDL_Delay(
            16
        );
    }

    SYS_Report(
        "DoomCube: orange save-file-created screen dismissed\n"
    );
}


void GC_LauncherRunStoragePreflight(
    SDL_Renderer *renderer)
{
    gc_memcard_status_t status;

    SDL_Rect panel;

    int width = 640;
    int height = 480;

    int textWidth;

    int i;

    if (!renderer)
    {
        return;
    }

    status =
        GC_MemoryCardGetStatus();

    if (status !=
            GC_MEMCARD_STATUS_TOO_SMALL &&
        status !=
            GC_MEMCARD_STATUS_NEEDS_CREATE)
    {
        return;
    }

    if (SDL_GetRendererOutputSize(
            renderer,
            &width,
            &height) != 0)
    {
        width = 640;
        height = 480;
    }

    SDL_SetRenderDrawColor(
        renderer,
        0,
        0,
        0,
        255
    );

    SDL_RenderClear(
        renderer
    );

    panel.x = 35;
    panel.y = 55;
    panel.w = width - 70;
    panel.h = height - 110;

    SDL_SetRenderDrawColor(
        renderer,
        70,
        45,
        120,
        255
    );

    SDL_RenderFillRect(
        renderer,
        &panel
    );

    SDL_SetRenderDrawColor(
        renderer,
        255,
        255,
        255,
        255
    );


    if (status ==
        GC_MEMCARD_STATUS_TOO_SMALL)
    {
        /*
         * Card59 warning uses dedicated red panel.
         *
         * Purple is reserved for player choices and orange for
         * successful state changes.
         */
        SDL_SetRenderDrawColor(
            renderer,
            150,
            25,
            25,
            255
        );

        SDL_RenderFillRect(
            renderer,
            &panel
        );

        SDL_SetRenderDrawColor(
            renderer,
            255,
            255,
            255,
            255
        );

        static const char *line1 =
            "MEMORY CARD 59 IS TOO SMALL";

        static const char *line2 =
            "DOOMCUBE REQUIRES MEMORY CARD 251";

        static const char *line3 =
            "OR LARGER";

        static const char *line4 =
            "SAVING IS DISABLED";

        static const char *line5 =
            "PRESS A OR START TO CONTINUE";

        textWidth =
            GC_TextWidth(
                line1,
                3
            );

        GC_DrawText(
            renderer,
            (width - textWidth) / 2,
            100,
            line1,
            3
        );

        textWidth =
            GC_TextWidth(
                line2,
                2
            );

        GC_DrawText(
            renderer,
            (width - textWidth) / 2,
            180,
            line2,
            2
        );

        textWidth =
            GC_TextWidth(
                line3,
                2
            );

        GC_DrawText(
            renderer,
            (width - textWidth) / 2,
            215,
            line3,
            2
        );

        textWidth =
            GC_TextWidth(
                line4,
                2
            );

        GC_DrawText(
            renderer,
            (width - textWidth) / 2,
            275,
            line4,
            2
        );

        textWidth =
            GC_TextWidth(
                line5,
                2
            );

        GC_DrawText(
            renderer,
            (width - textWidth) / 2,
            350,
            line5,
            2
        );

        SDL_RenderPresent(
            renderer
        );

        SYS_Report(
            "DoomCube: pre-launch Memory Card 59 warning shown\n"
        );

        for (i = 0;
             i < 3;
             ++i)
        {
            PAD_ScanPads();

            (void)PAD_ButtonsDown(
                PAD_CHAN0
            );

            SDL_Delay(
                16
            );
        }

        while (SYS_MainLoop())
        {
            u16 down;

            PAD_ScanPads();

            down =
                PAD_ButtonsDown(
                    PAD_CHAN0
                );

            if (down &
                (
                    PAD_BUTTON_A |
                    PAD_BUTTON_START
                ))
            {
                break;
            }

            SDL_Delay(
                16
            );
        }

        SYS_Report(
            "DoomCube: pre-launch Memory Card 59 warning dismissed\n"
        );

        return;
    }


    if (status ==
        GC_MEMCARD_STATUS_NEEDS_CREATE)
    {
        static const char *line1 =
            "NO DOOMCUBE SAVE FILE FOUND";

        static const char *line2 =
            "CREATE A SAVE FILE FOR DOOMCUBE";

        static const char *line5 =
            "A  CREATE";

        static const char *line6 =
            "B  CONTINUE WITHOUT SAVING";

        char initialLine[64];
        char maximumLine[64];

        snprintf(
            initialLine,
            sizeof(initialLine),
            "STARTS AT %u BLOCKS",
            (unsigned int)
                GC_MemoryCardSaveFileInitialBlocks()
        );

        snprintf(
            maximumLine,
            sizeof(maximumLine),
            "CAN GROW UP TO %u BLOCKS",
            (unsigned int)
                GC_MemoryCardSaveFileMaxBlocks()
        );

        textWidth =
            GC_TextWidth(
                line1,
                3
            );

        GC_DrawText(
            renderer,
            (width - textWidth) / 2,
            85,
            line1,
            3
        );

        textWidth =
            GC_TextWidth(
                line2,
                2
            );

        GC_DrawText(
            renderer,
            (width - textWidth) / 2,
            160,
            line2,
            2
        );

        textWidth =
            GC_TextWidth(
                initialLine,
                2
            );

        GC_DrawText(
            renderer,
            (width - textWidth) / 2,
            215,
            initialLine,
            2
        );

        textWidth =
            GC_TextWidth(
                maximumLine,
                2
            );

        GC_DrawText(
            renderer,
            (width - textWidth) / 2,
            250,
            maximumLine,
            2
        );

        textWidth =
            GC_TextWidth(
                line5,
                2
            );

        GC_DrawText(
            renderer,
            (width - textWidth) / 2,
            315,
            line5,
            2
        );

        textWidth =
            GC_TextWidth(
                line6,
                2
            );

        GC_DrawText(
            renderer,
            (width - textWidth) / 2,
            350,
            line6,
            2
        );

        SDL_RenderPresent(
            renderer
        );

        SYS_Report(
            "DoomCube: pre-launch save-file creation prompt shown: "
            "initial=%u maximum=%u blocks\n",
            (unsigned int)
                GC_MemoryCardSaveFileInitialBlocks(),
            (unsigned int)
                GC_MemoryCardSaveFileMaxBlocks()
        );

        for (i = 0;
             i < 3;
             ++i)
        {
            PAD_ScanPads();

            (void)PAD_ButtonsDown(
                PAD_CHAN0
            );

            SDL_Delay(
                16
            );
        }

        while (SYS_MainLoop())
        {
            u16 down;

            PAD_ScanPads();

            down =
                PAD_ButtonsDown(
                    PAD_CHAN0
                );

            if (down &
                PAD_BUTTON_A)
            {
                SYS_Report(
                    "DoomCube: player selected CREATE save file\n"
                );

                if (GC_MemoryCardCreateSaveFile())
                {
                    SYS_Report(
                        "DoomCube: pre-launch DoomCube save-file "
                        "creation succeeded\n"
                    );

                    GC_LauncherShowSaveFileCreated(
                        renderer
                    );

                    return;
                }

                /*
                 * Creation failed. Continue without saving rather than
                 * trapping the player before the launcher.
                 */
                SYS_Report(
                    "DoomCube: pre-launch DoomCube save-file "
                    "creation failed; saving disabled\n"
                );

                GC_MemoryCardShutdown();

                return;
            }

            if (down &
                PAD_BUTTON_B)
            {
                SYS_Report(
                    "DoomCube: player declined DoomCube save-file "
                    "creation; saving disabled for this session\n"
                );

                GC_MemoryCardShutdown();

                return;
            }

            SDL_Delay(
                16
            );
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

    if (!GC_LauncherRunSplash(
            renderer,
            logo))
    {
        if (logo != NULL)
        {
            SDL_DestroyTexture(logo);
        }

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
            int selectedBase =
                GC_LauncherRunCustomBase(
                    renderer);

            if (selectedBase == -2)
            {
                /*
                 * B returns to the normal launcher.
                 */
                continue;
            }

            if (selectedBase < 0)
            {
                DC_WARN(
                    "DoomCube: CUSTOM has no selectable base\n");

                continue;
            }

            /*
             * Atom 7 checkpoint:
             * prove CUSTOM -> SELECT BASE before attaching the
             * filtered PWAD selector.
             */
            DC_INFO(
                "DoomCube: CUSTOM checkpoint selected base %s\n",
                gcCustomBases[selectedBase].name);

            continue;
        }


        selection->iwadPath =
            game->iwadPath;
        selection->pwadPath =
            game->pwadPath;


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
        }

        return true;
    }

    if (logo != NULL)
    {
        SDL_DestroyTexture(logo);
    }

    return false;
}
