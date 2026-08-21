//
// w_file_embedded.c
//
// Dolphin-only embedded WAD backend for DoomCube.
//
// This file:
//
//   1. Presents bin2o's embedded doom1.wad through Doom's existing
//      wad_file_t interface.
//
//   2. Wraps M_FileExists() so Doom's IWAD discovery sees the
//      embedded doom1.wad.
//
// Upstream Doom source remains completely untouched.
//

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "doomtype.h"
#include "m_misc.h"
#include "w_file.h"
#include "z_zone.h"

#include "doom1_wad.h"


typedef struct
{
    wad_file_t wad;
} embedded_wad_file_t;


/*
 * Upstream w_file.c expects this exact symbol.
 *
 * In normal builds it comes from w_file_stdc.c.
 * In EMBED_WAD builds we provide it here instead.
 */
extern wad_file_class_t stdc_wad_file;


/* ------------------------------------------------------------------------- */
/* Filename helper                                                           */
/* ------------------------------------------------------------------------- */

static const char *GetFilename(const char *path)
{
    const char *slash;

    if (path == NULL)
    {
        return NULL;
    }

    slash = strrchr(path, '/');

    if (slash != NULL)
    {
        return slash + 1;
    }

    return path;
}


/* ------------------------------------------------------------------------- */
/* M_FileExists linker wrapper                                               */
/* ------------------------------------------------------------------------- */

/*
 * GNU ld --wrap=M_FileExists causes:
 *
 *     M_FileExists(...)
 *
 * to call:
 *
 *     __wrap_M_FileExists(...)
 *
 * Calling __real_M_FileExists() from here invokes Doom's original,
 * untouched M_FileExists().
 */
extern boolean __real_M_FileExists(char *filename);


boolean __wrap_M_FileExists(char *filename)
{
    const char *base;

    base = GetFilename(filename);

    if (base != NULL && strcasecmp(base, "doom1.wad") == 0)
    {
        printf(
            "DoomCube: embedded M_FileExists(%s) -> true\n",
            filename
        );

        return true;
    }

    /*
     * Anything other than our embedded IWAD behaves exactly
     * as upstream Doom intended.
     */
    return __real_M_FileExists(filename);
}


/* ------------------------------------------------------------------------- */
/* Embedded wad_file_t backend                                               */
/* ------------------------------------------------------------------------- */

static wad_file_t *W_Embedded_OpenFile(char *path)
{
    embedded_wad_file_t *result;
    const char *filename;

    filename = GetFilename(path);

    if (filename == NULL)
    {
        return NULL;
    }

    if (strcasecmp(filename, "doom1.wad") != 0)
    {
        return NULL;
    }


    printf(
        "DoomCube: opening embedded %s (%u bytes)\n",
        filename,
        (unsigned int)doom1_wad_size
    );


    result = Z_Malloc(
        sizeof(embedded_wad_file_t),
        PU_STATIC,
        NULL
    );


    if (result == NULL)
    {
        return NULL;
    }


    result->wad.file_class = &stdc_wad_file;

    /*
     * The WAD already resides in the DOL.
     */
    result->wad.mapped =
        (byte *)doom1_wad;

    result->wad.length =
        doom1_wad_size;


    return &result->wad;
}


static void W_Embedded_CloseFile(wad_file_t *wad)
{
    if (wad != NULL)
    {
        Z_Free(wad);
    }
}


static size_t W_Embedded_Read(
    wad_file_t *wad,
    unsigned int offset,
    void *buffer,
    size_t buffer_len
)
{
    size_t remaining;


    if (wad == NULL || buffer == NULL)
    {
        return 0;
    }


    if (offset >= wad->length)
    {
        return 0;
    }


    remaining =
        wad->length - offset;


    if (buffer_len > remaining)
    {
        buffer_len = remaining;
    }


    memcpy(
        buffer,
        wad->mapped + offset,
        buffer_len
    );


    return buffer_len;
}


/*
 * Deliberately named stdc_wad_file.
 *
 * Upstream w_file.c already references this symbol.
 */
wad_file_class_t stdc_wad_file =
{
    W_Embedded_OpenFile,
    W_Embedded_CloseFile,
    W_Embedded_Read
};