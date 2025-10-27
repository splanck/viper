//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the minimal debug-print helpers required by the BASIC runtime.  The
// functions in this translation unit intentionally flush @c stdout after every
// emission so integration tests and scripts that observe the native runtime
// receive deterministic output ordering.  The routines never take ownership of
// the passed-in buffers; they simply bridge to the platform C stdio
// implementation while guarding against null pointers.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Debug-print helpers shared by the BASIC runtime and developer tools.
/// @details Defines newline-terminated integer and string printers that flush
///          @c stdout to keep host-visible transcripts in sync with the VM's
///          behaviour.  The utilities intentionally avoid allocating so they
///          remain safe to call from low-level trap handlers.

#include "rt_debug.h"

#include <stdio.h>

/// @brief Print a signed 32-bit integer followed by a newline.
/// @details Forwards the value to @c printf using the @c "%d" format and
///          immediately flushes @c stdout so external harnesses observe the
///          result even when the process terminates abruptly.  The helper is
///          used primarily by deterministic integration tests that compare VM
///          and native output streams.
/// @param value Integer to print.
void rt_println_i32(int32_t value)
{
    printf("%d\n", value);
    fflush(stdout);
}

/// @brief Print a UTF-8 string followed by a newline.
/// @details Accepts borrowed storage, substitutes an empty string when passed
///          @c NULL, and flushes @c stdout after writing so the output buffer
///          never delays diagnostic emission.  The behaviour mirrors the
///          VM-side debug helpers, keeping the two runtimes interchangeable for
///          log comparisons.
/// @param text Null-terminated string to print; @c NULL is treated as empty.
void rt_println_str(const char *text)
{
    if (!text)
        text = "";
    printf("%s\n", text);
    fflush(stdout);
}
