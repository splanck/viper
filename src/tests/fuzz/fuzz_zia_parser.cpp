//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_zia_parser.cpp
// Purpose: libFuzzer harness for the Zia parser/compiler pipeline. Feeds
//          arbitrary bytes through the full compile() path (lex, parse,
//          sema, lower) to ensure no crashes on malformed input.
// Key invariants:
//   - Input size is capped at 16 KB to prevent OOM.
//   - The harness must return 0 for every input (no assertions).
//   - Compilation errors are expected and silently discarded.
// Ownership/Lifetime:
//   - All objects are stack-allocated and destroyed each iteration.
// Links: src/frontends/zia/Compiler.hpp, src/frontends/zia/Options.hpp
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "frontends/zia/Options.hpp"
#include "support/source_manager.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // Cap input size to 16 KB to prevent OOM on large corpus entries.
    constexpr size_t kMaxInputSize = 16 * 1024;
    if (size > kMaxInputSize)
        return 0;

    std::string source(reinterpret_cast<const char *>(data), size);

    SourceManager sm;
    CompilerInput input{};
    input.source = source;
    input.path = "<fuzz>";

    CompilerOptions options{};

    // Run the full compilation pipeline. Errors are expected on fuzz input
    // and are silently discarded — we only care about crashes.
    compile(input, options, sm);

    return 0;
}
