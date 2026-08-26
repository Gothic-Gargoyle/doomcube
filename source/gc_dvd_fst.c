#include "gc_dvd_fst.h"

#include <gccore.h>
#include <ogc/dvd.h>
#include <sys/iosupport.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>


#define GC_FST_ENTRY_SIZE 12u


typedef struct
{
    uint32_t offset;
    uint32_t size;
    uint32_t pos;
} gc_dvd_file_t;


typedef struct
{
    uint32_t type_name;
    uint32_t word1;
    uint32_t word2;
} gc_fst_entry_t;


static const gc_fst_entry_t *gc_fst;
static const char *gc_fst_strings;
static uint32_t gc_fst_count;
static bool gc_fst_mounted;


/*
 * GameCube low memory:
 *
 * 0x80000038 = FST pointer
 * 0x8000003c = FST maximum size
 *
 * Our apploader explicitly fills these before jumping to DoomCube.
 */
static const gc_fst_entry_t *GC_GetFST(void)
{
    volatile uint32_t *fst_ptr =
        (volatile uint32_t *)0x80000038;

    return (const gc_fst_entry_t *)(uintptr_t)(*fst_ptr);
}


static uint32_t GC_FST_TypeName(
    const gc_fst_entry_t *e)
{
    return e->type_name;
}


static bool GC_FST_IsDir(
    const gc_fst_entry_t *e)
{
    return (GC_FST_TypeName(e) & 0x01000000u) != 0;
}


static uint32_t GC_FST_NameOffset(
    const gc_fst_entry_t *e)
{
    return GC_FST_TypeName(e) & 0x00ffffffu;
}


static const char *GC_FST_Name(
    const gc_fst_entry_t *e)
{
    return gc_fst_strings +
        GC_FST_NameOffset(e);
}


static const char *GC_SkipDevicePrefix(
    const char *path)
{
    if (path == NULL)
        return NULL;

    if (strncmp(path, "dvd:", 4) == 0)
        path += 4;

    while (*path == '/')
        ++path;

    return path;
}


static bool GC_ComponentMatches(
    const char *component,
    size_t component_len,
    const char *name)
{
    size_t name_len;

    if (name == NULL)
        return false;

    name_len = strlen(name);

    if (name_len != component_len)
        return false;

    return strncasecmp(
        component,
        name,
        component_len) == 0;
}


static int GC_FindChild(
    uint32_t parent_index,
    const char *component,
    size_t component_len)
{
    const gc_fst_entry_t *parent;
    uint32_t i;
    uint32_t end;

    if (parent_index >= gc_fst_count)
        return -1;

    parent = &gc_fst[parent_index];

    if (!GC_FST_IsDir(parent))
        return -1;

    end = parent->word2;

    i = parent_index + 1;

    while (i < end && i < gc_fst_count)
    {
        const gc_fst_entry_t *e =
            &gc_fst[i];

        if (GC_ComponentMatches(
                component,
                component_len,
                GC_FST_Name(e)))
        {
            return (int)i;
        }

        /*
         * If this is a directory, word2 points to the
         * entry immediately following its descendants.
         */
        if (GC_FST_IsDir(e))
            i = e->word2;
        else
            ++i;
    }

    return -1;
}


static int GC_ResolvePath(
    const char *path)
{
    const char *p;
    uint32_t current = 0;

    p = GC_SkipDevicePrefix(path);

    if (p == NULL)
        return -1;

    if (*p == '\0')
        return 0;

    while (*p != '\0')
    {
        const char *slash;
        size_t len;
        int next;

        slash = strchr(p, '/');

        if (slash != NULL)
            len = (size_t)(slash - p);
        else
            len = strlen(p);

        if (len == 0)
        {
            if (slash == NULL)
                break;

            p = slash + 1;
            continue;
        }

        next = GC_FindChild(
            current,
            p,
            len);

        if (next < 0)
            return -1;

        current = (uint32_t)next;

        if (slash == NULL)
            break;

        p = slash + 1;
    }

    return (int)current;
}


static int GC_DVD_Open(
    struct _reent *r,
    void *fileStruct,
    const char *path,
    int flags,
    int mode)
{
    gc_dvd_file_t *f =
        (gc_dvd_file_t *)fileStruct;

    int index;

    (void)mode;

    if ((flags & O_ACCMODE) != O_RDONLY)
    {
        r->_errno = EROFS;
        return -1;
    }

    index = GC_ResolvePath(path);

    if (index < 0)
    {
        r->_errno = ENOENT;
        return -1;
    }

    if (GC_FST_IsDir(&gc_fst[index]))
    {
        r->_errno = EISDIR;
        return -1;
    }

    f->offset =
        gc_fst[index].word1;

    f->size =
        gc_fst[index].word2;

    f->pos = 0;

    return 0;
}


static int GC_DVD_Close(
    struct _reent *r,
    void *fd)
{
    (void)r;
    (void)fd;

    return 0;
}


static ssize_t GC_DVD_Read(
    struct _reent *r,
    void *fd,
    char *ptr,
    size_t len)
{
    gc_dvd_file_t *f =
        (gc_dvd_file_t *)fd;

    dvdcmdblk block;

    uint32_t remaining;
    uint32_t amount;

    s32 rc;

    if (f->pos >= f->size)
        return 0;

    remaining =
        f->size - f->pos;

    amount =
        len < remaining
            ? (uint32_t)len
            : remaining;

    if (amount == 0)
        return 0;

    /*
     * DVD DMA requires 32-byte alignment for the destination,
     * transfer length, AND physical disc offset.
     *
     * stdio may turn a tiny fread() into an aligned internal-buffer
     * refill. Therefore destination/length alignment alone is not
     * sufficient: an unaligned logical seek must still use the
     * bounce-buffer path.
     */
    if ((((uintptr_t)ptr) & 31u) == 0 &&
        (amount & 31u) == 0 &&
        (((f->offset + f->pos) & 31u) == 0))
    {
        rc = DVD_ReadAbsPrio(
            &block,
            ptr,
            amount,
            (s64)f->offset + f->pos,
            2);

        if (rc < 0)
        {
            r->_errno = EIO;
            return -1;
        }
    }
    else
    {
        uint32_t first =
            (f->offset + f->pos) & ~31u;

        uint32_t last =
            ((f->offset + f->pos + amount + 31u)
             & ~31u);

        uint32_t read_len =
            last - first;

        uint8_t *tmp =
            memalign(32, read_len);

        uint32_t skip;

        if (tmp == NULL)
        {
            r->_errno = ENOMEM;
            return -1;
        }

        rc = DVD_ReadAbsPrio(
            &block,
            tmp,
            read_len,
            first,
            2);

        if (rc < 0)
        {
            free(tmp);
            r->_errno = EIO;
            return -1;
        }

        skip =
            (f->offset + f->pos) - first;

        memcpy(
            ptr,
            tmp + skip,
            amount);

        free(tmp);
    }

    f->pos += amount;

    return (ssize_t)amount;
}


static off_t GC_DVD_Seek(
    struct _reent *r,
    void *fd,
    off_t pos,
    int dir)
{
    gc_dvd_file_t *f =
        (gc_dvd_file_t *)fd;

    int64_t next;

    switch (dir)
    {
        case SEEK_SET:
            next = pos;
            break;

        case SEEK_CUR:
            next =
                (int64_t)f->pos + pos;
            break;

        case SEEK_END:
            next =
                (int64_t)f->size + pos;
            break;

        default:
            r->_errno = EINVAL;
            return (off_t)-1;
    }

    if (next < 0 ||
        next > (int64_t)f->size)
    {
        r->_errno = EINVAL;
        return (off_t)-1;
    }

    f->pos = (uint32_t)next;

    return (off_t)f->pos;
}


static int GC_DVD_Fstat(
    struct _reent *r,
    void *fd,
    struct stat *st)
{
    gc_dvd_file_t *f =
        (gc_dvd_file_t *)fd;

    (void)r;

    memset(st, 0, sizeof(*st));

    st->st_mode =
        S_IFREG | 0444;

    st->st_size =
        f->size;

    return 0;
}


static int GC_DVD_Stat(
    struct _reent *r,
    const char *path,
    struct stat *st)
{
    int index =
        GC_ResolvePath(path);

    if (index < 0)
    {
        r->_errno = ENOENT;
        return -1;
    }

    memset(st, 0, sizeof(*st));

    if (GC_FST_IsDir(&gc_fst[index]))
    {
        st->st_mode =
            S_IFDIR | 0555;

        st->st_size = 0;
    }
    else
    {
        st->st_mode =
            S_IFREG | 0444;

        st->st_size =
            gc_fst[index].word2;
    }

    return 0;
}


static const devoptab_t gc_dvd_devoptab =
{
    .name       = "dvd",
    .structSize = sizeof(gc_dvd_file_t),

    .open_r     = GC_DVD_Open,
    .close_r    = GC_DVD_Close,
    .read_r     = GC_DVD_Read,
    .seek_r     = GC_DVD_Seek,

    .fstat_r    = GC_DVD_Fstat,
    .stat_r     = GC_DVD_Stat,
};


bool GC_DVDFST_Mount(void)
{
    const gc_fst_entry_t *root;
    uint32_t count;

    if (gc_fst_mounted)
        return true;

    gc_fst = GC_GetFST();

    if (gc_fst == NULL)
        return false;

    root = &gc_fst[0];

    if (!GC_FST_IsDir(root))
        return false;

    count = root->word2;

    if (count == 0 ||
        count > 100000u)
    {
        return false;
    }

    gc_fst_count = count;

    gc_fst_strings =
        (const char *)gc_fst +
        ((size_t)gc_fst_count *
         GC_FST_ENTRY_SIZE);

    DVD_Init();

    /*
     * In Dolphin this is harmless; on real hardware this prepares
     * the DVD subsystem for raw absolute reads.
     */
    DVD_Mount();

    if (AddDevice(&gc_dvd_devoptab) < 0)
        return false;

    gc_fst_mounted = true;

    return true;
}


void GC_DVDFST_Unmount(void)
{
    if (!gc_fst_mounted)
        return;

    RemoveDevice("dvd:");

    gc_fst = NULL;
    gc_fst_strings = NULL;
    gc_fst_count = 0;

    gc_fst_mounted = false;
}
