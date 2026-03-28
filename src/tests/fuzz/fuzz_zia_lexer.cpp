//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_zia_lexer.cpp
// Purpose: libFuzzer harness for the Zia lexer. Feeds arbitrary bytes into
//          the lexer and drains the token stream to EOF, ensuring the lexer
//          never crashes or hangs on any input.
// Key invariants:
//   - Input size is capped at 16 KB to prevent OOM.
//   - The harness must return 0 for every input (no assertions).
//   - The lexer must terminate (reach Eof) for every finite input.
// Ownership/Lifetime:
//   - All objects are stack-allocated and destroyed each iteration.
// Links: src/frontends/zia/Lexer.hpp, src/frontends/zia/Token.hpp
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lexer.hpp"
#include "support/diagnostics.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Cap input size to 16 KB to prevent OOM on large corpus entries.
    constexpr size_t kMaxInputSize = 16 * 1024;
    if (size > kMaxInputSize)
        return 0;

    std::string source(reinterpret_cast<const char *>(data), size);

    DiagnosticEngine diag;
    Lexer lexer(source, /*fileId=*/0, diag);

    // Drain the entire token stream. The lexer must always terminate.
    for (;;) {
        Token tok = lexer.next();
        if (tok.kind == TokenKind::Eof)
            break;
    }

    return 0;
}
