//
// DoomCube DVD-backed WAD file backend with a small multi-block LRU cache.
//
// Large IWADs remain on dvd:/ instead of being copied entirely into RAM.
// Random WAD access is serviced through multiple 64 KiB cache blocks so
// frequently revisited regions remain resident without consuming much RAM.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ogcsys.h>

#include "m_misc.h"
#include "w_file.h"
#include "z_zone.h"


#define GAMECUBE_WAD_CACHE_BLOCK_SIZE  (64u * 1024u)
#define GAMECUBE_WAD_CACHE_BLOCK_COUNT 16u


typedef struct
{
    unsigned char *data;

    unsigned int offset;
    size_t length;

    unsigned int last_used;

    int valid;
} gamecube_wad_cache_block_t;


typedef struct
{
    wad_file_t wad;

    FILE *fstream;

    gamecube_wad_cache_block_t
        cache[GAMECUBE_WAD_CACHE_BLOCK_COUNT];

    unsigned int use_counter;
} gamecube_wad_file_t;


/*
 * Important:
 *
 * w_file.c already expects this symbol to exist.
 * We intentionally keep the name "stdc_wad_file" so Doom core
 * requires no changes.
 */
extern wad_file_class_t stdc_wad_file;


/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static int W_GameCube_Seek(
    gamecube_wad_file_t *gamecube_wad,
    unsigned int offset)
{
    if (gamecube_wad == NULL ||
        gamecube_wad->fstream == NULL)
    {
        return 0;
    }

    if (fseek(
            gamecube_wad->fstream,
            (long)offset,
            SEEK_SET) != 0)
    {
        SYS_Report(
            "DoomCube: WAD seek failed at offset %u\n",
            offset);

        return 0;
    }

    return 1;
}


static size_t W_GameCube_DirectRead(
    gamecube_wad_file_t *gamecube_wad,
    unsigned int offset,
    void *buffer,
    size_t buffer_len)
{
    if (!W_GameCube_Seek(
            gamecube_wad,
            offset))
    {
        return 0;
    }

    return fread(
        buffer,
        1,
        buffer_len,
        gamecube_wad->fstream);
}


static gamecube_wad_cache_block_t *W_GameCube_FindCachedBlock(
    gamecube_wad_file_t *gamecube_wad,
    unsigned int offset)
{
    unsigned int i;

    for (i = 0;
         i < GAMECUBE_WAD_CACHE_BLOCK_COUNT;
         ++i)
    {
        gamecube_wad_cache_block_t *block =
            &gamecube_wad->cache[i];

        if (!block->valid)
            continue;

        if (offset >= block->offset &&
            offset < block->offset + block->length)
        {
            return block;
        }
    }

    return NULL;
}


static gamecube_wad_cache_block_t *W_GameCube_SelectCacheBlock(
    gamecube_wad_file_t *gamecube_wad)
{
    unsigned int i;

    gamecube_wad_cache_block_t *oldest =
        &gamecube_wad->cache[0];

    for (i = 0;
         i < GAMECUBE_WAD_CACHE_BLOCK_COUNT;
         ++i)
    {
        gamecube_wad_cache_block_t *block =
            &gamecube_wad->cache[i];

        if (!block->valid)
            return block;

        if (block->last_used <
            oldest->last_used)
        {
            oldest = block;
        }
    }

    return oldest;
}


static gamecube_wad_cache_block_t *W_GameCube_LoadCacheBlock(
    gamecube_wad_file_t *gamecube_wad,
    unsigned int offset)
{
    gamecube_wad_cache_block_t *block;

    unsigned int block_offset;

    size_t wanted;
    size_t bytes_read;

    block_offset =
        offset -
        (offset %
         GAMECUBE_WAD_CACHE_BLOCK_SIZE);

    if (block_offset >=
        gamecube_wad->wad.length)
    {
        return NULL;
    }

    block =
        W_GameCube_SelectCacheBlock(
            gamecube_wad);

    wanted =
        gamecube_wad->wad.length -
        block_offset;

    if (wanted >
        GAMECUBE_WAD_CACHE_BLOCK_SIZE)
    {
        wanted =
            GAMECUBE_WAD_CACHE_BLOCK_SIZE;
    }

    bytes_read =
        W_GameCube_DirectRead(
            gamecube_wad,
            block_offset,
            block->data,
            wanted);

    if (bytes_read == 0)
    {
        block->valid = 0;
        block->length = 0;

        return NULL;
    }

    block->offset =
        block_offset;

    block->length =
        bytes_read;

    block->last_used =
        ++gamecube_wad->use_counter;

    block->valid = 1;

    return block;
}


static gamecube_wad_cache_block_t *W_GameCube_GetCacheBlock(
    gamecube_wad_file_t *gamecube_wad,
    unsigned int offset)
{
    gamecube_wad_cache_block_t *block;

    block =
        W_GameCube_FindCachedBlock(
            gamecube_wad,
            offset);

    if (block == NULL)
    {
        block =
            W_GameCube_LoadCacheBlock(
                gamecube_wad,
                offset);
    }

    if (block != NULL)
    {
        block->last_used =
            ++gamecube_wad->use_counter;
    }

    return block;
}


/* ------------------------------------------------------------------------- */
/* Open                                                                      */
/* ------------------------------------------------------------------------- */

static wad_file_t *W_GameCube_OpenFile(
    char *path)
{
    FILE *fstream;

    gamecube_wad_file_t *result;

    long length;

    unsigned int i;

    SYS_Report(
        "DoomCube: opening DVD-backed WAD: %s\n",
        path);

    fstream =
        fopen(
            path,
            "rb");

    if (fstream == NULL)
    {
        SYS_Report(
            "DoomCube: failed opening WAD: %s\n",
            path);

        return NULL;
    }

    if (fseek(
            fstream,
            0,
            SEEK_END) != 0)
    {
        SYS_Report(
            "DoomCube: WAD seek-to-end failed\n");

        fclose(
            fstream);

        return NULL;
    }

    length =
        ftell(
            fstream);

    if (length <= 0)
    {
        SYS_Report(
            "DoomCube: invalid WAD size: %ld\n",
            length);

        fclose(
            fstream);

        return NULL;
    }

    if (fseek(
            fstream,
            0,
            SEEK_SET) != 0)
    {
        SYS_Report(
            "DoomCube: WAD rewind failed\n");

        fclose(
            fstream);

        return NULL;
    }

    result =
        Z_Malloc(
            sizeof(gamecube_wad_file_t),
            PU_STATIC,
            NULL);

    if (result == NULL)
    {
        SYS_Report(
            "DoomCube: failed allocating WAD handle\n");

        fclose(
            fstream);

        return NULL;
    }

    memset(
        result,
        0,
        sizeof(*result));

    for (i = 0;
         i < GAMECUBE_WAD_CACHE_BLOCK_COUNT;
         ++i)
    {
        result->cache[i].data =
            malloc(
                GAMECUBE_WAD_CACHE_BLOCK_SIZE);

        if (result->cache[i].data == NULL)
        {
            unsigned int j;

            SYS_Report(
                "DoomCube: failed allocating WAD cache block %u\n",
                i);

            for (j = 0;
                 j < i;
                 ++j)
            {
                free(
                    result->cache[j].data);

                result->cache[j].data =
                    NULL;
            }

            Z_Free(
                result);

            fclose(
                fstream);

            return NULL;
        }
    }

    result->fstream =
        fstream;

    result->wad.file_class =
        &stdc_wad_file;

    /*
     * Keep mapped NULL so Doom calls our Read() implementation instead
     * of assuming the complete IWAD is memory-mapped.
     */
    result->wad.mapped =
        NULL;

    result->wad.length =
        (unsigned int)length;

    result->use_counter =
        0;

    SYS_Report(
        "DoomCube: WAD size: %ld bytes\n",
        length);

    SYS_Report(
        "DoomCube: WAD cache: %u x %u KiB = %u KiB\n",
        GAMECUBE_WAD_CACHE_BLOCK_COUNT,
        GAMECUBE_WAD_CACHE_BLOCK_SIZE / 1024u,
        (GAMECUBE_WAD_CACHE_BLOCK_COUNT *
         GAMECUBE_WAD_CACHE_BLOCK_SIZE) / 1024u);

    return &result->wad;
}


/* ------------------------------------------------------------------------- */
/* Close                                                                     */
/* ------------------------------------------------------------------------- */

static void W_GameCube_CloseFile(
    wad_file_t *wad)
{
    gamecube_wad_file_t *gamecube_wad;

    unsigned int i;

    if (wad == NULL)
        return;

    gamecube_wad =
        (gamecube_wad_file_t *)wad;

    if (gamecube_wad->fstream != NULL)
    {
        fclose(
            gamecube_wad->fstream);

        gamecube_wad->fstream =
            NULL;
    }

    for (i = 0;
         i < GAMECUBE_WAD_CACHE_BLOCK_COUNT;
         ++i)
    {
        if (gamecube_wad->cache[i].data != NULL)
        {
            free(
                gamecube_wad->cache[i].data);

            gamecube_wad->cache[i].data =
                NULL;
        }

        gamecube_wad->cache[i].valid =
            0;
    }

    gamecube_wad->wad.mapped =
        NULL;

    Z_Free(
        gamecube_wad);
}


/* ------------------------------------------------------------------------- */
/* Read                                                                      */
/* ------------------------------------------------------------------------- */

static size_t W_GameCube_Read(
    wad_file_t *wad,
    unsigned int offset,
    void *buffer,
    size_t buffer_len)
{
    gamecube_wad_file_t *gamecube_wad;

    unsigned char *output;

    size_t total_read;

    if (wad == NULL ||
        buffer == NULL ||
        buffer_len == 0)
    {
        return 0;
    }

    gamecube_wad =
        (gamecube_wad_file_t *)wad;

    if (gamecube_wad->fstream == NULL)
        return 0;

    if (offset >= wad->length)
        return 0;

    if (buffer_len >
        (size_t)(wad->length - offset))
    {
        buffer_len =
            (size_t)(wad->length - offset);
    }

    /*
     * For sufficiently large sequential reads, bypass the cache.
     *
     * Filling many 64 KiB cache blocks only to immediately consume them
     * once is slower and needlessly evicts useful random-access data.
     */
    if (buffer_len >=
        GAMECUBE_WAD_CACHE_BLOCK_SIZE * 2u)
    {
        return W_GameCube_DirectRead(
            gamecube_wad,
            offset,
            buffer,
            buffer_len);
    }

    output =
        (unsigned char *)buffer;

    total_read =
        0;

    while (total_read <
           buffer_len)
    {
        gamecube_wad_cache_block_t *block;

        unsigned int current_offset;

        size_t block_index;
        size_t available;
        size_t wanted;

        current_offset =
            offset +
            (unsigned int)total_read;

        block =
            W_GameCube_GetCacheBlock(
                gamecube_wad,
                current_offset);

        if (block == NULL)
            break;

        block_index =
            current_offset -
            block->offset;

        if (block_index >=
            block->length)
        {
            break;
        }

        available =
            block->length -
            block_index;

        wanted =
            buffer_len -
            total_read;

        if (wanted >
            available)
        {
            wanted =
                available;
        }

        memcpy(
            output + total_read,
            block->data + block_index,
            wanted);

        total_read +=
            wanted;
    }

    return total_read;
}


/* ------------------------------------------------------------------------- */
/* File class                                                                */
/* ------------------------------------------------------------------------- */

wad_file_class_t stdc_wad_file =
{
    W_GameCube_OpenFile,
    W_GameCube_CloseFile,
    W_GameCube_Read,
};