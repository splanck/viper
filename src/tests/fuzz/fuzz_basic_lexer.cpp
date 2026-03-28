//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_basic_lexer.cpp
// Purpose: libFuzzer harness for the BASIC lexer. Feeds arbitrary bytes and
//          drains the token stream to EOF.
// Key invariants:
//   - Input capped at 16 KB.
//   - Lexer must terminate for every finite input.
// Links: src/frontends/basic/Lexer.hpp
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lexer.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

using namespace il::frontends::basic;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    constexpr size_t kMaxInputSize = 16 * 1024;
    if (size > kMaxInputSize)
        return 0;

    std::string source(reinterpret_cast<const char *>(data), size);
    Lexer lexer(source, /*fileId=*/0);

    for (;;) {
        auto tok = lexer.nextToken();
        if (tok.type == TokenType::END_OF_FILE)
            break;
    }

    return 0;
}
