//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_lsp_jsonrpc.cpp
// Purpose: libFuzzer harness for LSP JSON-RPC body parsing.
// Key invariants:
//   - Input is capped at 64 KB.
//   - JSON syntax and JSON-RPC validation errors are non-crashing outcomes.
// Links: tools/lsp-common/Json.hpp, tools/lsp-common/JsonRpc.hpp
//
//===----------------------------------------------------------------------===//

#include "tools/lsp-common/Json.hpp"
#include "tools/lsp-common/JsonRpc.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    constexpr size_t kMaxInputSize = 64 * 1024;
    if (size > kMaxInputSize)
        return 0;

    try {
        std::string_view body(reinterpret_cast<const char *>(data), size);
        auto value = zanna::server::JsonValue::parse(body);
        zanna::server::JsonRpcRequest request;
        if (zanna::server::parseRequest(value, request) && request.hasId) {
            (void)zanna::server::buildResponse(request.id, zanna::server::JsonValue());
            (void)zanna::server::buildError(request.id,
                                           zanna::server::kInvalidRequest,
                                           "fuzz");
        }
    } catch (const std::exception &) {
    }
    return 0;
}
