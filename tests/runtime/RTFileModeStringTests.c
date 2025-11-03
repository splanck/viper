// File: tests/runtime/RTFileModeStringTests.c
// Purpose: Validate that BASIC OPEN mode enumerations map to the expected mode strings.
// Key invariants: Returned mode strings are stable literals that include required modifiers.
// Ownership/Lifetime: Uses constant literals provided by the runtime; no allocations performed.
// Links: docs/codemap.md

#include "viper/runtime/rt.h"
#include "rt_file_path.h"

#include <assert.h>
#include <string.h>

static void assert_mode_literal(int32_t mode, const char *expected)
{
    const char *mode_literal = rt_file_mode_string(mode);
    assert(mode_literal != NULL);
    assert(strcmp(mode_literal, expected) == 0);
}

int main(void)
{
    assert_mode_literal(RT_F_INPUT, "r");
    assert_mode_literal(RT_F_OUTPUT, "w");
    assert_mode_literal(RT_F_APPEND, "a");

    const char *binary_literal = rt_file_mode_string(RT_F_BINARY);
    assert(binary_literal != NULL);
    assert(strcmp(binary_literal, "rbc+") == 0);

    const char *random_literal = rt_file_mode_string(RT_F_RANDOM);
    assert(random_literal != NULL);
    assert(strcmp(random_literal, "rbc+") == 0);

#if defined(_WIN32)
    // Windows requires the binary modifier to avoid text translation of newlines.
    assert(strchr(binary_literal, 'b') != NULL);
    assert(strchr(random_literal, 'b') != NULL);
#endif

    return 0;
}
