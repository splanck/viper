//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_binfile.c
// Purpose: Implement binary file stream operations.
//
// Open modes:
//   "r"  - Read only (rb)
//   "w"  - Write only (wb, truncates)
//   "rw" - Read/write (r+b)
//   "a"  - Append (ab)
//
//===----------------------------------------------------------------------===//

#include "rt_binfile.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief Bytes implementation structure (must match rt_bytes.c).
typedef struct rt_bytes_impl
{
    int64_t len;   ///< Number of bytes.
    uint8_t *data; ///< Byte storage.
} rt_bytes_impl;

/// @brief BinFile implementation structure.
typedef struct rt_binfile_impl
{
    FILE *fp;      ///< File pointer.
    int8_t eof;    ///< EOF flag.
    int8_t closed; ///< Closed flag.
} rt_binfile_impl;

static void rt_binfile_finalize(void *obj)
{
    if (!obj)
        return;
    rt_binfile_impl *bf = (rt_binfile_impl *)obj;
    if (bf->fp && !bf->closed)
    {
        fclose(bf->fp);
        bf->fp = NULL;
        bf->closed = 1;
    }
}

void *rt_binfile_open(void *path, void *mode)
{
    if (!path || !mode)
    {
        rt_trap("BinFile.Open: null path or mode");
        return NULL;
    }

    const char *path_str = rt_string_cstr((rt_string)path);
    const char *mode_str = rt_string_cstr((rt_string)mode);

    if (!path_str || !mode_str)
    {
        rt_trap("BinFile.Open: invalid path or mode");
        return NULL;
    }

    // Map mode string to fopen mode
    const char *fmode = NULL;
    if (strcmp(mode_str, "r") == 0)
        fmode = "rb";
    else if (strcmp(mode_str, "w") == 0)
        fmode = "wb";
    else if (strcmp(mode_str, "rw") == 0)
        fmode = "r+b";
    else if (strcmp(mode_str, "a") == 0)
        fmode = "ab";
    else
    {
        rt_trap("BinFile.Open: invalid mode (use r, w, rw, or a)");
        return NULL;
    }

    FILE *fp = fopen(path_str, fmode);
    if (!fp)
    {
        rt_trap("BinFile.Open: failed to open file");
        return NULL;
    }

    rt_binfile_impl *bf =
        (rt_binfile_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_binfile_impl));
    if (!bf)
    {
        fclose(fp);
        rt_trap("BinFile.Open: memory allocation failed");
        return NULL;
    }

    bf->fp = fp;
    bf->eof = 0;
    bf->closed = 0;
    rt_obj_set_finalizer(bf, rt_binfile_finalize);

    return bf;
}

void rt_binfile_close(void *obj)
{
    if (!obj)
        return;

    rt_binfile_impl *bf = (rt_binfile_impl *)obj;
    if (bf->fp && !bf->closed)
    {
        fclose(bf->fp);
        bf->fp = NULL;
        bf->closed = 1;
    }
}

int64_t rt_binfile_read(void *obj, void *bytes, int64_t offset, int64_t count)
{
    if (!obj)
    {
        rt_trap("BinFile.Read: null file");
        return 0;
    }
    if (!bytes)
    {
        rt_trap("BinFile.Read: null bytes");
        return 0;
    }

    rt_binfile_impl *bf = (rt_binfile_impl *)obj;
    if (!bf->fp || bf->closed)
    {
        rt_trap("BinFile.Read: file is closed");
        return 0;
    }

    rt_bytes_impl *b = (rt_bytes_impl *)bytes;
    if (offset < 0)
        offset = 0;
    if (count <= 0)
        return 0;
    if (offset >= b->len)
        return 0;

    // Clamp count to available space
    if (offset + count > b->len)
        count = b->len - offset;

    size_t read = fread(b->data + offset, 1, (size_t)count, bf->fp);

    // Update EOF flag
    if (feof(bf->fp))
        bf->eof = 1;

    return (int64_t)read;
}

void rt_binfile_write(void *obj, void *bytes, int64_t offset, int64_t count)
{
    if (!obj)
    {
        rt_trap("BinFile.Write: null file");
        return;
    }
    if (!bytes)
    {
        rt_trap("BinFile.Write: null bytes");
        return;
    }

    rt_binfile_impl *bf = (rt_binfile_impl *)obj;
    if (!bf->fp || bf->closed)
    {
        rt_trap("BinFile.Write: file is closed");
        return;
    }

    rt_bytes_impl *b = (rt_bytes_impl *)bytes;
    if (offset < 0)
        offset = 0;
    if (count <= 0)
        return;
    if (offset >= b->len)
        return;

    // Clamp count to available data
    if (offset + count > b->len)
        count = b->len - offset;

    size_t written = fwrite(b->data + offset, 1, (size_t)count, bf->fp);
    if (written < (size_t)count)
    {
        rt_trap("BinFile.Write: write failed");
    }
}

int64_t rt_binfile_read_byte(void *obj)
{
    if (!obj)
    {
        rt_trap("BinFile.ReadByte: null file");
        return -1;
    }

    rt_binfile_impl *bf = (rt_binfile_impl *)obj;
    if (!bf->fp || bf->closed)
    {
        rt_trap("BinFile.ReadByte: file is closed");
        return -1;
    }

    int c = fgetc(bf->fp);
    if (c == EOF)
    {
        bf->eof = 1;
        return -1;
    }

    return (int64_t)(unsigned char)c;
}

void rt_binfile_write_byte(void *obj, int64_t byte)
{
    if (!obj)
    {
        rt_trap("BinFile.WriteByte: null file");
        return;
    }

    rt_binfile_impl *bf = (rt_binfile_impl *)obj;
    if (!bf->fp || bf->closed)
    {
        rt_trap("BinFile.WriteByte: file is closed");
        return;
    }

    if (fputc((unsigned char)(byte & 0xFF), bf->fp) == EOF)
    {
        rt_trap("BinFile.WriteByte: write failed");
    }
}

int64_t rt_binfile_seek(void *obj, int64_t offset, int64_t origin)
{
    if (!obj)
    {
        rt_trap("BinFile.Seek: null file");
        return -1;
    }

    rt_binfile_impl *bf = (rt_binfile_impl *)obj;
    if (!bf->fp || bf->closed)
    {
        rt_trap("BinFile.Seek: file is closed");
        return -1;
    }

    int whence;
    switch (origin)
    {
    case 0:
        whence = SEEK_SET;
        break;
    case 1:
        whence = SEEK_CUR;
        break;
    case 2:
        whence = SEEK_END;
        break;
    default:
        rt_trap("BinFile.Seek: invalid origin (use 0, 1, or 2)");
        return -1;
    }

    if (fseek(bf->fp, (long)offset, whence) != 0)
    {
        return -1;
    }

    // Clear EOF flag after successful seek
    bf->eof = 0;
    clearerr(bf->fp);

    return (int64_t)ftell(bf->fp);
}

int64_t rt_binfile_pos(void *obj)
{
    if (!obj)
        return -1;

    rt_binfile_impl *bf = (rt_binfile_impl *)obj;
    if (!bf->fp || bf->closed)
        return -1;

    return (int64_t)ftell(bf->fp);
}

int64_t rt_binfile_size(void *obj)
{
    if (!obj)
        return -1;

    rt_binfile_impl *bf = (rt_binfile_impl *)obj;
    if (!bf->fp || bf->closed)
        return -1;

    // Save current position
    long pos = ftell(bf->fp);
    if (pos < 0)
        return -1;

    // Seek to end
    if (fseek(bf->fp, 0, SEEK_END) != 0)
        return -1;

    long size = ftell(bf->fp);

    // Restore position
    fseek(bf->fp, pos, SEEK_SET);

    return (int64_t)size;
}

void rt_binfile_flush(void *obj)
{
    if (!obj)
        return;

    rt_binfile_impl *bf = (rt_binfile_impl *)obj;
    if (!bf->fp || bf->closed)
        return;

    fflush(bf->fp);
}

int8_t rt_binfile_eof(void *obj)
{
    if (!obj)
        return 1;

    rt_binfile_impl *bf = (rt_binfile_impl *)obj;
    if (!bf->fp || bf->closed)
        return 1;

    return bf->eof;
}
