/* ------------------------------------------------------------------------- */
/* DoomCube launcher                                                         */
/* ------------------------------------------------------------------------- */

#include "gc_debug.h"

#include "gc_launcher.h"
#include "gc_memcard.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
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

#define GC_MAX_GAMES 5

typedef struct
{
    const char *name;
    const char *iwadPath;
    bool available;
    gc_savegame_id_t saveGameId;
} gc_game_entry_t;

static gc_game_entry_t gcGames[GC_MAX_GAMES] =
{
    { "DOOM SHAREWARE", "dvd:/data/wad/doom1.wad",    false, GC_SAVEGAME_DOOM1 },
    { "DOOM",           "dvd:/data/wad/doom.wad",     false, GC_SAVEGAME_DOOM },
    { "DOOM II",        "dvd:/data/wad/doom2.wad",    false, GC_SAVEGAME_DOOM2 },
    { "TNT: EVILUTION", "dvd:/data/wad/tnt.wad",      false, GC_SAVEGAME_TNT },
    { "PLUTONIA",       "dvd:/data/wad/plutonia.wad", false, GC_SAVEGAME_PLUTONIA }
};

static int gcAvailableGameCount;

static bool GC_FileExists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0;
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
        gcGames[i].available = GC_FileExists(gcGames[i].iwadPath);

        if (!gcGames[i].available)
            continue;

        ++gcAvailableGameCount;

        DC_DEBUG(
            "DoomCube: found %s (%s)\n",
            gcGames[i].name,
            gcGames[i].iwadPath);
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
    SDL_Texture *logo,
    int selected)
{
    int i;
    int shown = 0;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    /*
     * Build identification.
     *
     * Keep this small and unobtrusive in the bottom-right corner.
     * The strings are supplied by the Makefile.
     */
    {
        char versionText[96];
        int versionWidth;

        snprintf(
            versionText,
            sizeof(versionText),
            "DOOMCUBE V%s (%s)",
            DOOMCUBE_APP_VERSION,
            DOOMCUBE_GIT_ID);

        versionWidth =
            GC_TextWidth(versionText, 1);

        GC_DrawText(
            renderer,
            640 - versionWidth - 8,
            480 - 7 - 8,
            versionText,
            1);
    }

    if (logo != NULL)
    {
        int logoWidth;
        int logoHeight;
        SDL_Rect logoRect;

        if (SDL_QueryTexture(
                logo,
                NULL,
                NULL,
                &logoWidth,
                &logoHeight) == 0)
        {
            logoRect.x =
                (GC_LAUNCHER_WIDTH - logoWidth) / 2;

            logoRect.y =
                GC_LAUNCHER_LOGO_Y;

            logoRect.w =
                logoWidth;

            logoRect.h =
                logoHeight;

            SDL_RenderCopy(
                renderer,
                logo,
                NULL,
                &logoRect);
        }
        else
        {
            DC_WARN(
                "DoomCube: SDL_QueryTexture failed: %s\n",
                SDL_GetError());
        }
    }
    else
    {
        const char *title = "DOOMCUBE";
        int titleWidth = GC_TextWidth(title, 5);

        GC_DrawText(
            renderer,
            (GC_LAUNCHER_WIDTH - titleWidth) / 2,
            75,
            title,
            5);
    }

    for (i = 0; i < GC_MAX_GAMES; ++i)
    {
        int y;
        int textWidth;

        if (!gcGames[i].available)
            continue;

        y = 180 + shown * GC_LAUNCHER_LINE_HEIGHT;

        if (i == selected)
        {
            SDL_Rect marker = { 95, y - 5, 450, 28 };

            SDL_SetRenderDrawColor(renderer, 70, 45, 120, 255);
            SDL_RenderFillRect(renderer, &marker);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        }

        textWidth =
            GC_TextWidth(
                gcGames[i].name,
                GC_LAUNCHER_FONT_SCALE);

        GC_DrawText(
            renderer,
            (GC_LAUNCHER_WIDTH - textWidth) / 2,
            y,
            gcGames[i].name,
            GC_LAUNCHER_FONT_SCALE);

        ++shown;
    }

    {
        const char *startText = "A - START";
        const char *copyright =
            "@ COPYLEFT 2026 SPERGE BRIGADE STUDIOS";

        GC_DrawText(
            renderer,
            (GC_LAUNCHER_WIDTH - GC_TextWidth(startText, 2)) / 2,
            395,
            startText,
            2);

        GC_DrawText(
            renderer,
            (GC_LAUNCHER_WIDTH - GC_TextWidth(copyright, 2)) / 2,
            425,
            copyright,
            2);
    }

    SDL_RenderPresent(renderer);
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

static int GC_LauncherRun(
    SDL_Renderer *renderer,
    SDL_Texture *logo)
{
    int selected;
    int stickHeld = 0;

    selected = GC_FirstAvailableGame();

    if (selected < 0)
        return -1;

    GC_DrawLauncher(renderer, logo, selected);

    /*
     * Flush stale controller transition state before entering the launcher.
     */
    for (int i = 0; i < 3; ++i)
    {
        PAD_ScanPads();
        (void)PAD_ButtonsDown(0);
        SDL_Delay(16);
    }

    DC_DEBUG(
        "DoomCube: entering launcher input loop\n");

    while (SYS_MainLoop())
    {
        u16 down;
        s8 stickY;
        int stickDirection = 0;

        PAD_ScanPads();

        down = PAD_ButtonsDown(0);
        stickY = PAD_StickY(0);

        if (stickY > GC_LAUNCHER_DEADZONE)
            stickDirection = 1;
        else if (stickY < -GC_LAUNCHER_DEADZONE)
            stickDirection = -1;

        if ((down & PAD_BUTTON_UP) ||
            (stickDirection > 0 && !stickHeld))
        {
            int next = GC_NextAvailableGame(selected, -1);

            if (next >= 0)
            {
                selected = next;
                GC_DrawLauncher(renderer, logo, selected);
            }
        }

        if ((down & PAD_BUTTON_DOWN) ||
            (stickDirection < 0 && !stickHeld))
        {
            int next = GC_NextAvailableGame(selected, 1);

            if (next >= 0)
            {
                selected = next;
                GC_DrawLauncher(renderer, logo, selected);
            }
        }

        stickHeld = stickDirection != 0;

        if (down & (PAD_BUTTON_A | PAD_BUTTON_START))
{
    /*
     * Present a loading indication before returning from the launcher.
     * IWAD/game initialization happens after this function returns and
     * can take long enough to otherwise look like a freeze.
     */
    GC_DrawLoadingOverlay(renderer);

    GC_MemoryCardSetGame(gcGames[selected].saveGameId);

    DC_DEBUG(
        "DoomCube: launcher selected %s\n",
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

const char *GC_LauncherSelectGame(SDL_Renderer *renderer)
{
    int availableGames;
    int selectedGame;
    const gc_game_entry_t *game;
    SDL_Texture *logo;

    availableGames =
        GC_LauncherScanGames();

    if (availableGames == 0)
    {
        DC_WARN(
            "DoomCube: no supported IWADs found on disc\n");

        return NULL;
    }

    logo = GC_LoadLauncherLogo(renderer);

    selectedGame =
        GC_LauncherRun(renderer, logo);

    if (logo != NULL)
    {
        SDL_DestroyTexture(logo);
    }

    if (selectedGame < 0)
    {
        return NULL;
    }

    game =
        GC_LauncherGetGame(selectedGame);

    if (!game)
    {
        DC_WARN(
            "DoomCube: invalid launcher selection\n");

        return NULL;
    }

    DC_INFO(
        "DoomCube: launcher selected %s (%s)\n",
        game->name,
        game->iwadPath);

    return game->iwadPath;
}