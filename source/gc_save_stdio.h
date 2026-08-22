#ifndef DOOMCUBE_GC_SAVE_STDIO_H
#define DOOMCUBE_GC_SAVE_STDIO_H

#include <stdio.h>
#include <stddef.h>

FILE *GC_SaveFOpen(
    const char *path,
    const char *mode
);

size_t GC_SaveFRead(
    void *ptr,
    size_t size,
    size_t nmemb,
    FILE *stream
);

size_t GC_SaveFWrite(
    const void *ptr,
    size_t size,
    size_t nmemb,
    FILE *stream
);

int GC_SaveFClose(
    FILE *stream
);

long GC_SaveFTell(
    FILE *stream
);

int GC_SaveRemove(
    const char *path
);

int GC_SaveRename(
    const char *oldpath,
    const char *newpath
);

#ifdef DOOMCUBE_SAVE_SHIM

#define fopen   GC_SaveFOpen
#define fread   GC_SaveFRead
#define fwrite  GC_SaveFWrite
#define fclose  GC_SaveFClose
#define ftell   GC_SaveFTell
#define remove  GC_SaveRemove
#define rename  GC_SaveRename

#endif

#endif