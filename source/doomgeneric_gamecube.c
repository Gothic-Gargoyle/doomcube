#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>

#include "doomkeys.h"
#include "doomgeneric.h"
#include "m_controls.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ogcsys.h>
#include <gccore.h>

#include <iso9660.h>
#include <ogc/dvd.h>

#define KEYQUEUE_SIZE      64
#define STICK_DEADZONE     24
#define CSTICK_DEADZONE    24
#define TRIGGER_THRESHOLD  40
#define GC_KEY_PREVWEAPON 0xa4
#define GC_KEY_NEXTWEAPON 0xa5

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;

static unsigned short keyQueue[KEYQUEUE_SIZE];

static unsigned int keyWrite;
static unsigned int keyRead;

static int gcUp;
static int gcDown;
static int gcLeft;
static int gcRight;

static int gcFire;
static int gcUse;
static int gcRun;

static int gcEnter;
static int gcEscape;
static int gcTab;

static int gcStrafeLeft;
static int gcStrafeRight;

static int gcPrevWeapon;
static int gcNextWeapon;

static bool dvdMounted;


/* ------------------------------------------------------------------------- */
/* Key queue                                                                 */
/* ------------------------------------------------------------------------- */

static void queueKey(int pressed, unsigned char key)
{
    unsigned int next = (keyWrite + 1) % KEYQUEUE_SIZE;

    if (next == keyRead)
        keyRead = (keyRead + 1) % KEYQUEUE_SIZE;

    keyQueue[keyWrite] = ((pressed ? 1 : 0) << 8) | key;
    keyWrite = next;
}

static void setKeyState(int wanted, int *state, unsigned char key)
{
    wanted = !!wanted;

    if (*state == wanted)
        return;

    *state = wanted;
    queueKey(wanted, key);
}


/* ------------------------------------------------------------------------- */
/* GameCube controller                                                       */
/* ------------------------------------------------------------------------- */

static void handleGameCubeInput(void)
{
    PAD_ScanPads();

    u16 held = PAD_ButtonsHeld(0);

    s8 stickX = PAD_StickX(0);
    s8 stickY = PAD_StickY(0);
    s8 cstickX = PAD_SubStickX(0);

    u8 triggerR = PAD_TriggerR(0);

    int up = (held & PAD_BUTTON_UP) || stickY > STICK_DEADZONE;
    int down = (held & PAD_BUTTON_DOWN) || stickY < -STICK_DEADZONE;
    int left = (held & PAD_BUTTON_LEFT) || stickX < -STICK_DEADZONE;
    int right = (held & PAD_BUTTON_RIGHT) || stickX > STICK_DEADZONE;

    int cLeft = cstickX < -CSTICK_DEADZONE;
    int cRight = cstickX > CSTICK_DEADZONE;

    setKeyState(up, &gcUp, KEY_UPARROW);
    setKeyState(down, &gcDown, KEY_DOWNARROW);
    setKeyState(left, &gcLeft, KEY_LEFTARROW);
    setKeyState(right, &gcRight, KEY_RIGHTARROW);

    setKeyState(cLeft, &gcStrafeLeft, KEY_STRAFE_L);
    setKeyState(cRight, &gcStrafeRight, KEY_STRAFE_R);

    setKeyState(triggerR > TRIGGER_THRESHOLD, &gcFire, KEY_FIRE);

    setKeyState(held & PAD_BUTTON_X, &gcNextWeapon, key_nextweapon);
    setKeyState(held & PAD_BUTTON_Y, &gcPrevWeapon, key_prevweapon);

    setKeyState(held & PAD_BUTTON_A, &gcUse, KEY_USE);
    setKeyState(held & PAD_TRIGGER_L, &gcRun, KEY_RSHIFT);

    setKeyState(held & PAD_BUTTON_A, &gcEnter, KEY_ENTER);
    setKeyState(held & PAD_BUTTON_START, &gcEscape, KEY_ESCAPE);
    setKeyState(held & PAD_TRIGGER_Z, &gcTab, KEY_TAB);
}


/* ------------------------------------------------------------------------- */
/* ISO9660                                                                   */
/* ------------------------------------------------------------------------- */

static bool mountIsoFilesystem(void)
{
    printf("DoomCube: mounting ISO9660...\n");

    if (!ISO9660_Mount("dvd", &__io_gcdvd))
    {
        printf("DoomCube: ISO9660_Mount FAILED\n");
        return false;
    }

    dvdMounted = true;

    printf("DoomCube: mounted dvd:/\n");

    return true;
}


/* ------------------------------------------------------------------------- */
/* DVD WAD probe                                                             */
/* ------------------------------------------------------------------------- */

static uint32_t readLE32(const unsigned char *p)
{
    return ((uint32_t)p[0])
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static void probeDvdWad(void)
{
    FILE *file;
    long fileSize;

    unsigned char header[12];
    unsigned char entry[16];

    uint32_t numLumps;
    uint32_t directoryOffset;

    printf("\n");
    printf("DoomCube: ---- DVD WAD PROBE ----\n");

    file = fopen("dvd:/doom1.wad", "rb");

    if (!file)
    {
        printf("DoomCube: fopen FAILED\n");
        return;
    }

    printf("DoomCube: fopen OK\n");

    if (fseek(file, 0, SEEK_END) != 0)
    {
        printf("DoomCube: SEEK_END FAILED\n");
        fclose(file);
        return;
    }

    fileSize = ftell(file);

    if (fileSize < 0)
    {
        printf("DoomCube: ftell FAILED\n");
        fclose(file);
        return;
    }

    printf("DoomCube: file size = %ld bytes\n", fileSize);

    if (fseek(file, 0, SEEK_SET) != 0)
    {
        printf("DoomCube: SEEK_SET(0) FAILED\n");
        fclose(file);
        return;
    }

    if (fread(header, 1, sizeof(header), file) != sizeof(header))
    {
        printf("DoomCube: header fread FAILED\n");
        fclose(file);
        return;
    }

    printf("DoomCube: identification = %.4s\n", header);

    if (memcmp(header, "IWAD", 4) != 0
     && memcmp(header, "PWAD", 4) != 0)
    {
        printf("DoomCube: invalid WAD identification\n");
        fclose(file);
        return;
    }

    numLumps = readLE32(header + 4);
    directoryOffset = readLE32(header + 8);

    printf("DoomCube: numlumps = %u\n", (unsigned int)numLumps);
    printf(
        "DoomCube: directory offset = 0x%08x\n",
        (unsigned int)directoryOffset
    );

    if (numLumps == 0)
    {
        printf("DoomCube: invalid zero-lump WAD\n");
        fclose(file);
        return;
    }

    if ((long)directoryOffset >= fileSize)
    {
        printf("DoomCube: directory offset outside file\n");
        fclose(file);
        return;
    }

    if (fseek(file, (long)directoryOffset, SEEK_SET) != 0)
    {
        printf("DoomCube: directory fseek FAILED\n");
        fclose(file);
        return;
    }

    if (fread(entry, 1, sizeof(entry), file) != sizeof(entry))
    {
        printf("DoomCube: directory fread FAILED\n");
        fclose(file);
        return;
    }

    printf("DoomCube: first lump: %.8s\n", entry + 8);

    printf(
        "DoomCube: first lump offset = 0x%08x\n",
        (unsigned int)readLE32(entry)
    );

    printf(
        "DoomCube: first lump size = %u\n",
        (unsigned int)readLE32(entry + 4)
    );

    {
        uint32_t lastEntryOffset =
            directoryOffset + ((numLumps - 1) * 16);

        if (fseek(file, (long)lastEntryOffset, SEEK_SET) != 0)
        {
            printf("DoomCube: last-entry fseek FAILED\n");
            fclose(file);
            return;
        }

        if (fread(entry, 1, sizeof(entry), file) != sizeof(entry))
        {
            printf("DoomCube: last-entry fread FAILED\n");
            fclose(file);
            return;
        }

        printf("DoomCube: last lump: %.8s\n", entry + 8);

        printf(
            "DoomCube: last lump offset = 0x%08x\n",
            (unsigned int)readLE32(entry)
        );

        printf(
            "DoomCube: last lump size = %u\n",
            (unsigned int)readLE32(entry + 4)
        );
    }

    fclose(file);

    printf("DoomCube: DVD WAD PROBE SUCCESS\n");
    printf("DoomCube: -----------------------\n\n");
}


/* ------------------------------------------------------------------------- */
/* DoomGeneric                                                               */
/* ------------------------------------------------------------------------- */

void DG_Init(void)
{
    PAD_Init();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        exit(1);
    }

    window = SDL_CreateWindow(
        "DOOM",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        DOOMGENERIC_RESX,
        DOOMGENERIC_RESY,
        SDL_WINDOW_SHOWN
    );

    if (!window)
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        exit(1);
    }

    renderer = SDL_CreateRenderer(window, -1, 0);

    if (!renderer)
    {
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        exit(1);
    }

    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB888,
        SDL_TEXTUREACCESS_STREAMING,
        DOOMGENERIC_RESX,
        DOOMGENERIC_RESY
    );

    if (!texture)
    {
        printf("SDL_CreateTexture failed: %s\n", SDL_GetError());
        exit(1);
    }

    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    if (!mountIsoFilesystem())
    {
        printf("DoomCube: disc mount unavailable\n");
        return;
    }

    probeDvdWad();
}


void DG_DrawFrame(void)
{
    SDL_UpdateTexture(
        texture,
        NULL,
        DG_ScreenBuffer,
        DOOMGENERIC_RESX * sizeof(uint32_t)
    );

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);

    handleGameCubeInput();
}


void DG_SleepMs(uint32_t ms)
{
    SDL_Delay(ms);
}


uint32_t DG_GetTicksMs(void)
{
    return SDL_GetTicks();
}


int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    unsigned short data;

    if (keyRead == keyWrite)
        return 0;

    data = keyQueue[keyRead];
    keyRead = (keyRead + 1) % KEYQUEUE_SIZE;

    *pressed = data >> 8;
    *doomKey = data & 0xff;

    return 1;
}


void DG_SetWindowTitle(const char *title)
{
    if (window)
        SDL_SetWindowTitle(window, title);
}


/* ------------------------------------------------------------------------- */
/* Main                                                                      */
/* ------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char *doomArgv[] =
    {
        "doomcube",
        "-iwad",
        "doom1.wad"
    };

    doomgeneric_Create(3, doomArgv);


    key_prevweapon = GC_KEY_PREVWEAPON;
    key_nextweapon = GC_KEY_NEXTWEAPON;

    while (SYS_MainLoop())
        doomgeneric_Tick();

    if (dvdMounted)
        ISO9660_Unmount("dvd");

    return 0;
}