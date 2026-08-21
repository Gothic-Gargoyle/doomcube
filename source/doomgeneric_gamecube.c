// doomgeneric_gamecube.c
//
// GameCube SDL2 framebuffer test.
//
// ONLY this file needs to be compiled.
//
// Purpose:
//   CPU 320x200 framebuffer
//          -> SDL texture
//          -> SDL renderer
//          -> GameCube display

#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <ogcsys.h>
#include <gccore.h>

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 200

static SDL_Window   *window   = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture  *texture  = NULL;

static uint32_t testFramebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];

static void DrawTestPattern(uint32_t frame)
{
    for (int y = 0; y < SCREEN_HEIGHT; y++)
    {
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            uint8_t r = (uint8_t)(x + frame);
            uint8_t g = (uint8_t)(y + frame);
            uint8_t b = (uint8_t)(x + y + frame);

            testFramebuffer[y * SCREEN_WIDTH + x] =
                ((uint32_t)r << 16) |
                ((uint32_t)g << 8)  |
                ((uint32_t)b);
        }
    }
}

static int InitialiseSDL(void)
{
    printf("Calling SDL_Init...\n");

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }

    printf("SDL_Init succeeded.\n");

    printf("Creating SDL window...\n");

    window = SDL_CreateWindow(
        "DOOMCUBE",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        SDL_WINDOW_SHOWN
    );

    if (window == NULL)
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 0;
    }

    printf("SDL_CreateWindow succeeded.\n");

    printf("Creating SDL renderer...\n");

    renderer = SDL_CreateRenderer(
        window,
        -1,
        0
    );

    if (renderer == NULL)
    {
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return 0;
    }

    printf("SDL_CreateRenderer succeeded.\n");

    printf("Creating 320x200 streaming texture...\n");

    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    );

    if (texture == NULL)
    {
        printf("SDL_CreateTexture failed: %s\n", SDL_GetError());
        return 0;
    }

    printf("SDL_CreateTexture succeeded.\n");

    return 1;
}

static int RenderFrame(void)
{
    if (SDL_UpdateTexture(
            texture,
            NULL,
            testFramebuffer,
            SCREEN_WIDTH * sizeof(uint32_t)
        ) < 0)
    {
        printf("SDL_UpdateTexture failed: %s\n", SDL_GetError());
        return 0;
    }

    if (SDL_RenderClear(renderer) < 0)
    {
        printf("SDL_RenderClear failed: %s\n", SDL_GetError());
        return 0;
    }

    if (SDL_RenderCopy(
            renderer,
            texture,
            NULL,
            NULL
        ) < 0)
    {
        printf("SDL_RenderCopy failed: %s\n", SDL_GetError());
        return 0;
    }

    SDL_RenderPresent(renderer);

    return 1;
}

static void ShutdownSDL(void)
{
    if (texture != NULL)
    {
        SDL_DestroyTexture(texture);
        texture = NULL;
    }

    if (renderer != NULL)
    {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }

    if (window != NULL)
    {
        SDL_DestroyWindow(window);
        window = NULL;
    }

    SDL_Quit();
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    PAD_Init();

    printf("\n");
    printf("================================\n");
    printf("DOOMCUBE SDL FRAMEBUFFER TEST\n");
    printf("================================\n");
    printf("\n");

    if (!InitialiseSDL())
    {
        printf("SDL initialisation failed.\n");

        while (SYS_MainLoop())
        {
            PAD_ScanPads();

            if (PAD_ButtonsDown(0) & PAD_BUTTON_START)
            {
                break;
            }
        }

        ShutdownSDL();
        return 1;
    }

    printf("\n");
    printf("Rendering test framebuffer.\n");
    printf("Press START to exit.\n");
    printf("\n");

    uint32_t frame = 0;

    while (SYS_MainLoop())
    {
        SDL_PumpEvents();

        DrawTestPattern(frame++);

        if (!RenderFrame())
        {
            printf("Rendering failed.\n");
            break;
        }

        PAD_ScanPads();

        if (PAD_ButtonsDown(0) & PAD_BUTTON_START)
        {
            break;
        }

        SDL_Delay(16);
    }

    ShutdownSDL();

    return 0;
}