// File: tests/runtime/Int64ToStringTests.cpp
// Purpose: Lock down runtime formatting for critical 64-bit integer values.
// Key invariants: Decimal spellings are canonical and portable across toolchains.
// Ownership: Runtime integer formatting helpers.
// Links: docs/codemap.md

#include "rt_int_format.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>

int main()
{
    struct FormatCase
    {
        int64_t value;
        const char *expected;
    };

    const std::array<FormatCase, 7> cases = {
        {{0, "0"},
         {1, "1"},
         {-1, "-1"},
         {std::numeric_limits<int64_t>::max(), "9223372036854775807"},
         {std::numeric_limits<int64_t>::min(), "-9223372036854775808"},
         {static_cast<int64_t>(1000000000000000000LL), "1000000000000000000"},
         {static_cast<int64_t>(-1000000000000000000LL), "-1000000000000000000"}}};

    for (const auto &test : cases)
    {
        char buffer[64] = {};
        const size_t written = rt_i64_to_cstr(test.value, buffer, sizeof(buffer));
        assert(written == std::strlen(test.expected));
        assert(std::strcmp(buffer, test.expected) == 0);

        if (test.value >= 0)
        {
            char unsigned_buffer[64] = {};
            const size_t unsigned_written = rt_u64_to_cstr(
                static_cast<uint64_t>(test.value), unsigned_buffer, sizeof(unsigned_buffer));
            assert(unsigned_written == std::strlen(test.expected));
            assert(std::strcmp(unsigned_buffer, test.expected) == 0);
        }
    }

    return 0;
}
