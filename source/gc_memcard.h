#ifndef DOOMCUBE_GC_MEMCARD_H
#define DOOMCUBE_GC_MEMCARD_H

#include <stdbool.h>

bool GC_MemoryCardInit(void);
bool GC_MemoryCardWriteTest(void);
bool GC_MemoryCardReadTest(void);
void GC_MemoryCardShutdown(void);

#endif