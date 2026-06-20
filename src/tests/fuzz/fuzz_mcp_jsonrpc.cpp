//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_mcp_jsonrpc.cpp
// Purpose: libFuzzer harness for MCP newline-delimited JSON-RPC payload parsing.
// Key invariants:
//   - Input is capped at 64 KB.
//   - Each line may be malformed; parser exceptions are contained.
// Links: tools/lsp-common/Json.hpp, tools/lsp-common/JsonRpc.hpp
//
//===----------------------------------------------------------------------===//

#include "tools/lsp-common/Json.hpp"
#include "tools/lsp-common/JsonRpc.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    constexpr size_t kMaxInputSize = 64 * 1024;
    if (size > kMaxInputSize)
        return 0;

    std::string input(reinterpret_cast<const char *>(data), size);
    std::size_t start = 0;
    while (start <= input.size()) {
        std::size_t end = input.find('\n', start);
        if (end == std::string::npos)
            end = input.size();
        try {
            std::string_view line(input.data() + start, end - start);
            auto value = viper::server::JsonValue::parse(line);
            viper::server::JsonRpcRequest request;
            (void)viper::server::parseRequest(value, request);
        } catch (const std::exception &) {
        }
        if (end == input.size())
            break;
        start = end + 1;
    }
    return 0;
}
