//
// DoomCube RAM-backed WAD file backend.
//
// The IWAD is read from dvd:/ once at startup and kept in RAM.
// Doom's existing WAD code then performs all random accesses against
// the memory buffer instead of repeatedly seeking around the DVD.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m_misc.h"
#include "w_file.h"
#include "z_zone.h"


typedef struct
{
    wad_file_t wad;

    unsigned char *buffer;
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
/* Open                                                                      */
/* ------------------------------------------------------------------------- */

static wad_file_t *W_GameCube_OpenFile(char *path)
{
    FILE *fstream;

    gamecube_wad_file_t *result;

    long length;
    size_t bytesRead;

    printf(
        "DoomCube: loading WAD into RAM: %s\n",
        path
    );

    fstream = fopen(
        path,
        "rb"
    );

    if (fstream == NULL)
    {
        printf(
            "DoomCube: failed opening WAD: %s\n",
            path
        );

        return NULL;
    }


    /*
     * Determine WAD size.
     */
    if (fseek(
            fstream,
            0,
            SEEK_END) != 0)
    {
        printf(
            "DoomCube: WAD seek-to-end failed\n"
        );

        fclose(fstream);

        return NULL;
    }

    length = ftell(
        fstream
    );

    if (length <= 0)
    {
        printf(
            "DoomCube: invalid WAD size: %ld\n",
            length
        );

        fclose(fstream);

        return NULL;
    }

    if (fseek(
            fstream,
            0,
            SEEK_SET) != 0)
    {
        printf(
            "DoomCube: WAD rewind failed\n"
        );

        fclose(fstream);

        return NULL;
    }


    printf(
        "DoomCube: WAD size: %ld bytes\n",
        length
    );


    /*
     * Allocate the Doom wad_file wrapper.
     */
    result = Z_Malloc(
        sizeof(gamecube_wad_file_t),
        PU_STATIC,
        NULL
    );

    if (result == NULL)
    {
        printf(
            "DoomCube: failed allocating WAD handle\n"
        );

        fclose(fstream);

        return NULL;
    }

    memset(
        result,
        0,
        sizeof(*result)
    );


    /*
     * Allocate the actual WAD image outside the Doom zone.
     *
     * This remains resident for the lifetime of the WAD.
     */
    result->buffer = malloc(
        (size_t)length
    );

    if (result->buffer == NULL)
    {
        printf(
            "DoomCube: failed allocating %ld bytes for WAD\n",
            length
        );

        Z_Free(result);

        fclose(fstream);

        return NULL;
    }


    /*
     * Read the entire IWAD from DVD now.
     *
     * This is the only big WAD read we should need.
     */
    bytesRead = fread(
        result->buffer,
        1,
        (size_t)length,
        fstream
    );

    fclose(
        fstream
    );


    if (bytesRead != (size_t)length)
    {
        printf(
            "DoomCube: short WAD read: %u / %ld bytes\n",
            (unsigned int)bytesRead,
            length
        );

        free(
            result->buffer
        );

        Z_Free(
            result
        );

        return NULL;
    }


    /*
     * Fill Doom's normal wad_file_t structure.
     *
     * Setting mapped to the RAM image is useful because Doom's
     * W_Read() implementation can directly memcpy from this buffer.
     */
    result->wad.file_class =
        &stdc_wad_file;

    result->wad.mapped =
        result->buffer;

    result->wad.length =
        (unsigned int)length;


    printf(
        "DoomCube: WAD loaded into RAM at %p\n",
        result->buffer
    );

    printf(
        "DoomCube: DVD random WAD access eliminated\n"
    );


    return &result->wad;
}


/* ------------------------------------------------------------------------- */
/* Close                                                                     */
/* ------------------------------------------------------------------------- */

static void W_GameCube_CloseFile(wad_file_t *wad)
{
    gamecube_wad_file_t *gamecube_wad;

    if (wad == NULL)
        return;

    gamecube_wad =
        (gamecube_wad_file_t *)wad;

    if (gamecube_wad->buffer != NULL)
    {
        free(
            gamecube_wad->buffer
        );

        gamecube_wad->buffer = NULL;
    }

    gamecube_wad->wad.mapped =
        NULL;

    Z_Free(
        gamecube_wad
    );
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

    size_t available;

    gamecube_wad =
        (gamecube_wad_file_t *)wad;


    if (gamecube_wad == NULL ||
        gamecube_wad->buffer == NULL)
    {
        return 0;
    }


    if (offset >= wad->length)
        return 0;


    available =
        wad->length - offset;

    if (buffer_len > available)
        buffer_len = available;


    memcpy(
        buffer,
        gamecube_wad->buffer + offset,
        buffer_len
    );


    return buffer_len;
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