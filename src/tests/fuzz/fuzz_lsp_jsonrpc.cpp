//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
        auto value = viper::server::JsonValue::parse(body);
        viper::server::JsonRpcRequest request;
        if (viper::server::parseRequest(value, request) && request.hasId) {
            (void)viper::server::buildResponse(request.id, viper::server::JsonValue());
            (void)viper::server::buildError(request.id,
                                           viper::server::kInvalidRequest,
                                           "fuzz");
        }
    } catch (const std::exception &) {
    }
    return 0;
}
