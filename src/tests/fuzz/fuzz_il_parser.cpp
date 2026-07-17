//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_il_parser.cpp
// Purpose: libFuzzer harness for the IL text parser. Feeds arbitrary bytes
//          through the IL parser to find crashes.
// Key invariants:
//   - Input capped at 16 KB.
//   - Parser must not crash on any input.
// Links: il/io/Parser.hpp
//
//===----------------------------------------------------------------------===//

#include "il/core/Module.hpp"
#include "il/io/Parser.hpp"
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    constexpr size_t kMaxInputSize = 16 * 1024;
    if (size > kMaxInputSize)
        return 0;

    std::string source(reinterpret_cast<const char *>(data), size);
    std::istringstream input(source);
    il::core::Module module;
    (void)il::io::Parser::parse(input, module);

    return 0;
}
