//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_http_request.cpp
// Purpose: libFuzzer harness for HTTP request parsing.
// Key invariants:
//   - Input is capped at 64 KB.
//   - Malformed requests must be rejected without crashes or leaks.
// Links: src/runtime/network/rt_http_server.c
//
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" int rt_http_server_test_parse_request(const char *raw,
                                                 size_t raw_len,
                                                 char **method_out,
                                                 char **path_out,
                                                 char **body_out,
                                                 size_t *body_len_out);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    constexpr size_t kMaxInputSize = 64 * 1024;
    if (size > kMaxInputSize)
        return 0;

    char *method = nullptr;
    char *path = nullptr;
    char *body = nullptr;
    size_t bodyLen = 0;
    (void)rt_http_server_test_parse_request(reinterpret_cast<const char *>(data),
                                            size,
                                            &method,
                                            &path,
                                            &body,
                                            &bodyLen);
    std::free(method);
    std::free(path);
    std::free(body);
    return 0;
}
