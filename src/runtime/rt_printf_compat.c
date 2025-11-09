//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_printf_compat.c
// Purpose: Default definitions for printf-style wrappers used by the runtime.
//          Marked weak so tests can provide overrides without linker errors.
//
//===----------------------------------------------------------------------===//

#include "rt_printf_compat.h"

#include <stdarg.h>
#include <stdio.h>

__attribute__((weak)) int rt_snprintf(char *str, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return written;
}

