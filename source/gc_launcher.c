/* ------------------------------------------------------------------------- */
/* DoomCube launcher                                                         */
/* ------------------------------------------------------------------------- */

#include "gc_launcher.h"
#define GC_LAUNCHER_FONT_SCALE  3
#define GC_LAUNCHER_LINE_HEIGHT 32
#define GC_LAUNCHER_WIDTH       640
#define GC_LAUNCHER_DEADZONE    24

#include "gc_memcard.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include <gccore.h>
#include <ogcsys.h>

#define GC_LAUNCHER_FONT_SCALE  3
#define GC_LAUNCHER_LINE_HEIGHT 32
#define GC_LAUNCHER_WIDTH       640
#define GC_LAUNCHER_DEADZONE    24

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
    { "DOOM SHAREWARE", "dvd:/doom1.wad",    false, GC_SAVEGAME_DOOM1 },
    { "DOOM",           "dvd:/doom.wad",     false, GC_SAVEGAME_DOOM },
    { "DOOM II",        "dvd:/doom2.wad",    false, GC_SAVEGAME_DOOM2 },
    { "TNT: EVILUTION", "dvd:/tnt.wad",      false, GC_SAVEGAME_TNT },
    { "PLUTONIA",       "dvd:/plutonia.wad", false, GC_SAVEGAME_PLUTONIA }
};

static int gcAvailableGameCount;

static bool GC_FileExists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0;
}

static int GC_LauncherScanGames(void)
{
    int i;

    gcAvailableGameCount = 0;

    SYS_Report("DoomCube: ---- AVAILABLE GAMES ----\n");

    for (i = 0; i < GC_MAX_GAMES; ++i)
    {
        gcGames[i].available = GC_FileExists(gcGames[i].iwadPath);

        if (!gcGames[i].available)
            continue;

        ++gcAvailableGameCount;

        SYS_Report(
            "DoomCube: found %s (%s)\n",
            gcGames[i].name,
            gcGames[i].iwadPath);
    }

    SYS_Report(
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

    static const uint8_t colon[7] = { 0, 4, 4, 0, 4, 4, 0 };
    static const uint8_t dash[7]  = { 0, 0, 0, 31, 0, 0, 0 };

    unsigned char uc = (unsigned char)toupper((unsigned char)c);

    if (uc >= 'A' && uc <= 'Z')
        return glyphs[uc - 'A'];

    switch (uc)
    {
        case ':': return colon;
        case '-': return dash;
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

static void GC_DrawText(SDL_Renderer *renderer, int x, int y,
    const char *text, int scale)
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

static void GC_DrawLauncher(SDL_Renderer *renderer, int selected)
{
    int i;
    int shown = 0;
    const char *title = "DOOMCUBE";
    int titleWidth;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    titleWidth = GC_TextWidth(title, 5);

    GC_DrawText(
        renderer,
        (GC_LAUNCHER_WIDTH - titleWidth) / 2,
        75,
        title,
        5);

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

        textWidth = GC_TextWidth(gcGames[i].name, GC_LAUNCHER_FONT_SCALE);

        GC_DrawText(
            renderer,
            (GC_LAUNCHER_WIDTH - textWidth) / 2,
            y,
            gcGames[i].name,
            GC_LAUNCHER_FONT_SCALE);

        ++shown;
    }

    GC_DrawText(renderer, 205, 395, "A - START", 2);
    SDL_RenderPresent(renderer);
}

static int GC_LauncherRun(SDL_Renderer *renderer)
{
    int selected;
    int stickHeld = 0;

    selected = GC_FirstAvailableGame();

    if (selected < 0)
        return -1;

    GC_DrawLauncher(renderer, selected);

/*
 * Flush stale controller transition state before entering the launcher.
 */
for (int i = 0; i < 3; ++i)
{
    PAD_ScanPads();
    (void)PAD_ButtonsDown(0);
    SDL_Delay(16);
}

    SYS_Report(
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
                GC_DrawLauncher(renderer, selected);
            }
        }

        if ((down & PAD_BUTTON_DOWN) ||
            (stickDirection < 0 && !stickHeld))
        {
            int next = GC_NextAvailableGame(selected, 1);

            if (next >= 0)
            {
                selected = next;
                GC_DrawLauncher(renderer, selected);
            }
        }

        stickHeld = stickDirection != 0;

        if (down & (PAD_BUTTON_A | PAD_BUTTON_START))
        {
            GC_MemoryCardSetGame(gcGames[selected].saveGameId);
            SYS_Report(
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

    availableGames =
        GC_LauncherScanGames();

    if (availableGames == 0)
    {
        SYS_Report(
            "DoomCube: no supported IWADs found on disc\n");

        return NULL;
    }

    selectedGame =
        GC_LauncherRun(renderer);

    if (selectedGame < 0)
    {
        return NULL;
    }

    game =
        GC_LauncherGetGame(selectedGame);

    if (!game)
    {
        SYS_Report(
            "DoomCube: invalid launcher selection\n");

        return NULL;
    }

    SYS_Report(
        "DoomCube: launcher selected %s (%s)\n",
        game->name,
        game->iwadPath);

    return game->iwadPath;
}