//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Bridges BASIC runtime strings with raw byte buffers.  The helpers in this
// translation unit construct single-character strings, expose ASCII conversion
// utilities, and adapt literal C strings into the runtime's reference-counted
// representation.  Centralising the logic keeps ownership rules and error
// reporting consistent across VM and native integrations.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief String encoding helpers shared by the BASIC runtime.
/// @details Defines utility functions that convert between integers and
///          single-character strings, borrow C string views, and wrap literals
///          into @ref rt_string handles while preserving the runtime's memory
///          management conventions.

#include "rt_int_format.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/// @brief Construct a runtime string containing a single byte value.
/// @details Validates that @p code falls within the 0–255 range, formats an
///          informative trap message for out-of-range values, and delegates to
///          @ref rt_string_from_bytes to produce a reference-counted runtime
///          string containing the encoded character.
/// @param code Integer code point representing the desired byte.
/// @return Newly allocated runtime string containing the encoded byte.
rt_string rt_chr(int64_t code)
{
    if (code < 0 || code > 255)
    {
        char buf[64];
        char numbuf[32];
        rt_i64_to_cstr(code, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "CHR$: code must be 0-255 (got %s)", numbuf);
        rt_trap(buf);
    }
    char ch = (char)(unsigned char)code;
    return rt_string_from_bytes(&ch, 1);
}

/// @brief Extract the first byte of a runtime string as an integer.
/// @details Ensures the input handle and backing storage are valid before
///          returning the initial byte as an unsigned value.  Empty strings
///          produce zero, matching the legacy BASIC semantics.
/// @param s Runtime string handle to inspect.
/// @return Integer value of the first byte, or zero when the string is empty.
int64_t rt_asc(rt_string s)
{
    if (!s)
        rt_trap("rt_asc: null");
    size_t len = (size_t)rt_len(s);
    if (len == 0 || !s->data)
        return 0;
    return (int64_t)(unsigned char)s->data[0];
}

/// @brief Borrow a @c const char* view of a runtime-managed string.
/// @details Rejects null handles and null data pointers, reporting traps so
///          callers cannot inadvertently dereference invalid buffers.  The
///          returned pointer remains owned by the runtime; callers must not
///          mutate or free it.
/// @param s Runtime string handle to view.
/// @return Pointer to the underlying character array.
const char *rt_string_cstr(rt_string s)
{
    if (!s)
    {
        rt_trap("rt_string_cstr: null string");
        return "";
    }
    if (!s->data)
    {
        rt_trap("rt_string_cstr: null data");
        return "";
    }
    return s->data;
}

/// @brief Wrap a literal C string in a runtime string handle.
/// @details Allocates a minimal @ref rt_string structure that references the
///          caller-supplied character array without copying.  The wrapper marks
///          the storage as literal so the runtime avoids attempting to free it
///          during retain/release transitions.
/// @param c Null-terminated string owned by the caller.
/// @return Runtime string handle borrowing @p c, or @c NULL when @p c is null.
rt_string rt_const_cstr(const char *c)
{
    if (!c)
        return NULL;
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    if (!s)
    {
        rt_trap("rt_const_cstr: alloc");
        return NULL;
    }
    s->magic = RT_STRING_MAGIC;
    s->data = (char *)c;
    s->heap = NULL;
    s->literal_len = strlen(c);
    // BUG-VL-003 fix: Make literal strings immortal so they're never freed
    // This prevents use-after-free when string literals are used in loops
    s->literal_refs = SIZE_MAX;
    return s;
}
