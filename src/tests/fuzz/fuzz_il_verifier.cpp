//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_il_verifier.cpp
// Purpose: libFuzzer harness for the IL verifier. Parses arbitrary bytes as IL
//          text and, when they parse into a module, runs the full verifier so
//          the type, control-flow, SSA/dominance, and exception-handling checks
//          (the richest correctness surface, and previously unfuzzed) are
//          exercised on adversarial input.
// Key invariants:
//   - Input capped at 16 KB.
//   - Neither the parser nor the verifier may crash on any input; verification
//     failures are expected and silently discarded.
// Links: il/io/Parser.hpp, il/verify/Verifier.hpp
//
//===----------------------------------------------------------------------===//

#include "il/core/Module.hpp"
#include "il/io/Parser.hpp"
#include "il/verify/Verifier.hpp"
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
    // Only verify modules that parsed cleanly — a half-parsed module is not a
    // meaningful verifier input. The verifier must not crash on any parsed module.
    if (il::io::Parser::parse(input, module))
        (void)il::verify::Verifier::verify(module);

    return 0;
}
