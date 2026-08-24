#ifndef GC_DEBUG_H
#define GC_DEBUG_H

#include <ogc/system.h>

/*
 * DoomCube logging
 *
 * DC_LOG   - normal, useful release information
 * DC_WARN  - failures/warnings that should always be visible
 * DC_DEBUG - verbose development diagnostics; compiled out in release builds
 */

#define DC_LOG(...)  SYS_Report(__VA_ARGS__)
#define DC_WARN(...) SYS_Report(__VA_ARGS__)

#ifdef DOOMCUBE_DEBUG
#define DC_DEBUG(...) SYS_Report(__VA_ARGS__)
#else
#define DC_DEBUG(...) ((void)0)
#endif

#endif /* GC_DEBUG_H */