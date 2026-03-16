//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_basic_parser.cpp
// Purpose: libFuzzer harness for the BASIC parser. Feeds arbitrary bytes
//          through lexer and parser.
// Key invariants:
//   - Input capped at 16 KB.
//   - Parser must not crash on any input.
// Links: src/frontends/basic/Parser.hpp
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Parser.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    constexpr size_t kMaxInputSize = 16 * 1024;
    if (size > kMaxInputSize)
        return 0;

    std::string source(reinterpret_cast<const char *>(data), size);

    SourceManager sm;
    uint32_t fileId = sm.addFile("fuzz.bas");
    DiagnosticEngine eng;
    DiagnosticEmitter em(eng, sm);
    em.addSource(fileId, source);

    Parser parser(source.c_str(), fileId, &em);
    (void)parser.parseProgram();

    return 0;
}
