//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_locale_json_loader.cpp
// Purpose: libFuzzer harness for the MessageBundle JSON loader path. Feeds
//          arbitrary bytes as JSON text; the loader should either accept
//          and populate a bundle or trap cleanly on malformed input.
// Key invariants:
//   - Input capped at 64 KB.
//   - Trap recovery lets the fuzzer move on after a validation trap.
// Links: src/runtime/localization/rt_message_bundle.h,
//        src/runtime/text/rt_json.h (underlying parser)
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_locale.h"
#include "rt_message_bundle.h"
#include "rt_string.h"

#include <csetjmp>
#include <cstddef>
#include <cstdint>

extern "C" void *rt_json_parse_object(rt_string text);

static thread_local jmp_buf g_fuzz_env;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    constexpr size_t kMaxInputSize = 64 * 1024;
    if (size > kMaxInputSize)
        return 0;

    rt_string text = rt_string_from_bytes(reinterpret_cast<const char *>(data), size);

    rt_trap_set_recovery(&g_fuzz_env);
    if (setjmp(g_fuzz_env) == 0) {
        // Directly exercise the JSON object parser; the loader would call
        // this internally after reading the file bytes.
        void *map = rt_json_parse_object(text);
        (void)map;
    }
    rt_trap_set_recovery(nullptr);

    rt_string_unref(text);
    return 0;
}
