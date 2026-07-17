//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_date_pattern_parser.cpp
// Purpose: libFuzzer harness exercising Zanna.Localization.DateFormat.Custom.
//          Feeds arbitrary byte strings as CLDR pattern input against a
//          fixed reference timestamp; the interpreter should either format
//          a string or trap cleanly on unsupported/invalid patterns.
// Key invariants:
//   - Input capped at 1 KB (pattern cap is 256 bytes inside the runtime;
//     anything longer traps uniformly).
//   - Trap recovery via rt_trap_set_recovery so the fuzzer continues.
// Links: src/runtime/localization/rt_dateformat.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_dateformat.h"
#include "rt_datetime.h"
#include "rt_locale.h"
#include "rt_string.h"

#include <csetjmp>
#include <cstddef>
#include <cstdint>

static thread_local jmp_buf g_fuzz_env;
static thread_local int g_recovering = 0;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    constexpr size_t kMaxInputSize = 1024;
    if (size > kMaxInputSize)
        return 0;

    rt_string tag = rt_string_from_bytes("en-US", 5);
    void *loc = rt_locale_parse(tag);
    rt_string_unref(tag);
    void *fmt = rt_dateformat_for_locale(loc);
    int64_t ts = rt_datetime_create(2027, 3, 15, 14, 30, 5);

    rt_string pattern = rt_string_from_bytes(reinterpret_cast<const char *>(data), size);

    g_recovering = 1;
    rt_trap_set_recovery(&g_fuzz_env);
    if (setjmp(g_fuzz_env) == 0) {
        rt_string out = rt_dateformat_custom(fmt, ts, pattern);
        rt_string_unref(out);
    }
    rt_trap_set_recovery(nullptr);
    g_recovering = 0;

    rt_string_unref(pattern);
    return 0;
}
