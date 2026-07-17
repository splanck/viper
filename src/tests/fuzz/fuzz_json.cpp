//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_json.cpp
// Purpose: libFuzzer harness for the runtime JSON parser.
// Key invariants:
//   - Input is capped at 64 KB.
//   - Invalid JSON must report failure without crashing.
// Links: src/runtime/text/rt_json.h
//
//===----------------------------------------------------------------------===//

#include "rt_json.h"
#include "rt_string.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    constexpr size_t kMaxInputSize = 64 * 1024;
    if (size > kMaxInputSize)
        return 0;

    rt_string text = rt_string_from_bytes(reinterpret_cast<const char *>(data), size);
    void *value = nullptr;
    rt_string message = nullptr;
    int64_t line = 0;
    int64_t column = 0;
    (void)rt_json_try_parse(text, &value, &message, &line, &column);
    rt_string_unref(message);
    rt_string_unref(text);
    return 0;
}
