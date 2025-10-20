// File: tests/runtime/RTFileModeTests.c
// Purpose: Verify runtime file mode parsing maps binary modifiers to platform flags.
// Key invariants: Binary modifier must set the platform-specific binary bit on Windows builds.
// Ownership/Lifetime: Uses static mode strings; relies on runtime library for parsing.
// Links: docs/codemap.md
#include "rt_file_path.h"

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>

int main(void)
{
#ifdef _WIN32
    int flags = 0;
    assert(rt_file_mode_to_flags("rbc+", &flags));

    int binary_mask = 0;
#ifdef O_BINARY
    binary_mask |= O_BINARY;
#endif
#ifdef _O_BINARY
    binary_mask |= _O_BINARY;
#endif
    assert(binary_mask != 0);
    assert((flags & binary_mask) == binary_mask);

    int text_flags = 0;
    assert(rt_file_mode_to_flags("rt", &text_flags));
    assert((text_flags & binary_mask) == 0);
#else
    (void)rt_file_mode_to_flags;
#endif
    return 0;
}
