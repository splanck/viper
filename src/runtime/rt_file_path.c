// File: src/runtime/rt_file_path.c
// Purpose: Implement runtime helpers for resolving file paths and mode strings.
// Key invariants: Exposed helpers never modify the provided runtime strings.
// Ownership/Lifetime: Callers retain responsibility for the lifetime of ViperString handles.
// Links: docs/specs/errors.md

#include "rt_file_path.h"
#include "rt_file.h"

#include "rt_heap.h"
#include "rt_internal.h"

#include <fcntl.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

const char *rt_file_mode_string(int32_t mode)
{
    switch (mode)
    {
    case RT_F_INPUT:
        return "r";
    case RT_F_OUTPUT:
        return "w";
    case RT_F_APPEND:
        return "a";
    case RT_F_BINARY:
        return "rbc+";
    case RT_F_RANDOM:
        return "rbc+";
    default:
        return NULL;
    }
}

bool rt_file_mode_to_flags(const char *mode, int *flags_out)
{
    if (flags_out)
        *flags_out = 0;
    if (!mode || !mode[0] || !flags_out)
        return false;

    int flags = 0;
    switch (mode[0])
    {
    case 'r':
        flags = O_RDONLY;
        break;
    case 'w':
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        break;
    case 'a':
        flags = O_WRONLY | O_CREAT | O_APPEND;
        break;
    default:
        return false;
    }

    bool plus = false;
    bool create = false;
    bool binary = false;
    for (const char *p = mode + 1; *p; ++p)
    {
        if (*p == '+')
            plus = true;
        else if (*p == 'b')
            binary = true;
        else if (*p == 't')
            continue;
        else if (*p == 'c')
            create = true;
        else
            return false;
    }

    if (plus)
    {
        flags &= ~(O_RDONLY | O_WRONLY);
        flags |= O_RDWR;
    }
    if (create)
        flags |= O_CREAT;
#if defined(_WIN32)
    if (binary)
    {
#    if defined(O_BINARY)
        flags |= O_BINARY;
#    elif defined(_O_BINARY)
        flags |= _O_BINARY;
#    endif
    }
#else
    (void)binary;
#endif
    flags |= O_CLOEXEC;

    *flags_out = flags;
    return true;
}

bool rt_file_path_from_vstr(const ViperString *path, const char **out_path)
{
    if (out_path)
        *out_path = NULL;
    if (!path || !path->data)
        return false;
    if (out_path)
        *out_path = path->data;
    return true;
}

size_t rt_file_string_view(const ViperString *s, const uint8_t **data_out)
{
    if (data_out)
        *data_out = NULL;
    if (!s || !s->data)
        return 0;
    if (data_out)
        *data_out = (const uint8_t *)s->data;
    if (s->heap)
        return rt_heap_len(s->data);
    return s->literal_len;
}

