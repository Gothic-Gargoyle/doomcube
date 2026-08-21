//
// doomgeneric_gamecube.c
//
// GameCube DoomGeneric platform backend.
//
// GameCube-specific responsibilities only:
//   - SDL2 video
//   - timing
//   - input
//   - application entry point
//
// No upstream Doom source is modified.
//

#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>

#include "doomkeys.h"
#include "doomgeneric.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <ogcsys.h>
#include <gccore.h>


static SDL_Window   *window   = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture  *texture  = NULL;


#define KEYQUEUE_SIZE 16

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex  = 0;


static unsigned char convertToDoomKey(unsigned int key)
{
    switch (key)
    {
        case SDLK_RETURN: return KEY_ENTER;
        case SDLK_ESCAPE: return KEY_ESCAPE;

        case SDLK_LEFT:  return KEY_LEFTARROW;
        case SDLK_RIGHT: return KEY_RIGHTARROW;
        case SDLK_UP:    return KEY_UPARROW;
        case SDLK_DOWN:  return KEY_DOWNARROW;

        case SDLK_LCTRL:
        case SDLK_RCTRL:
            return KEY_FIRE;

        case SDLK_SPACE:
            return KEY_USE;

        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
            return KEY_RSHIFT;

        case SDLK_LALT:
        case SDLK_RALT:
            return KEY_LALT;

        case SDLK_F2:  return KEY_F2;
        case SDLK_F3:  return KEY_F3;
        case SDLK_F4:  return KEY_F4;
        case SDLK_F5:  return KEY_F5;
        case SDLK_F6:  return KEY_F6;
        case SDLK_F7:  return KEY_F7;
        case SDLK_F8:  return KEY_F8;
        case SDLK_F9:  return KEY_F9;
        case SDLK_F10: return KEY_F10;
        case SDLK_F11: return KEY_F11;

        case SDLK_EQUALS:
            return KEY_EQUALS;

        case SDLK_MINUS:
            return KEY_MINUS;

        default:
            return (unsigned char)tolower((int)key);
    }
}


static void addKeyToQueue(int pressed, unsigned int keyCode)
{
    unsigned char key = convertToDoomKey(keyCode);

    s_KeyQueue[s_KeyQueueWriteIndex] =
        ((pressed ? 1 : 0) << 8) | key;

    s_KeyQueueWriteIndex =
        (s_KeyQueueWriteIndex + 1)
        % KEYQUEUE_SIZE;
}


static void handleInput(void)
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_QUIT:
                exit(0);
                break;

            case SDL_KEYDOWN:
                if (!event.key.repeat)
                {
                    addKeyToQueue(
                        1,
                        event.key.keysym.sym
                    );
                }
                break;

            case SDL_KEYUP:
                addKeyToQueue(
                    0,
                    event.key.keysym.sym
                );
                break;

            default:
                break;
        }
    }
}


void DG_Init(void)
{
    PAD_Init();

    printf("DoomCube: SDL_Init\n");

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        printf(
            "SDL_Init failed: %s\n",
            SDL_GetError()
        );

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


    if (window == NULL)
    {
        printf(
            "SDL_CreateWindow failed: %s\n",
            SDL_GetError()
        );

        exit(1);
    }


    renderer = SDL_CreateRenderer(
        window,
        -1,
        0
    );


    if (renderer == NULL)
    {
        printf(
            "SDL_CreateRenderer failed: %s\n",
            SDL_GetError()
        );

        exit(1);
    }


    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB888,
        SDL_TEXTUREACCESS_STREAMING,
        DOOMGENERIC_RESX,
        DOOMGENERIC_RESY
    );


    if (texture == NULL)
    {
        printf(
            "SDL_CreateTexture failed: %s\n",
            SDL_GetError()
        );

        exit(1);
    }


    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    printf("DoomCube: video ready\n");
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

    SDL_RenderCopy(
        renderer,
        texture,
        NULL,
        NULL
    );

    SDL_RenderPresent(renderer);

    handleInput();
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
    unsigned short keyData;

    if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex)
    {
        return 0;
    }


    keyData =
        s_KeyQueue[s_KeyQueueReadIndex];


    s_KeyQueueReadIndex =
        (s_KeyQueueReadIndex + 1)
        % KEYQUEUE_SIZE;


    *pressed = keyData >> 8;
    *doomKey = keyData & 0xff;


    return 1;
}


void DG_SetWindowTitle(const char *title)
{
    if (window != NULL)
    {
        SDL_SetWindowTitle(window, title);
    }
}


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


    printf("DoomCube: starting DoomGeneric\n");


    doomgeneric_Create(
        3,
        doomArgv
    );


    printf("DoomCube: DoomGeneric initialized\n");


    while (SYS_MainLoop())
    {
        doomgeneric_Tick();
    }


    return 0;
}