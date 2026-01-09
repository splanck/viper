//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_global_keyword_boundary.cpp
// Purpose: Verify module parser distinguishes global directives from labels sharing the prefix.
// Key invariants: Only bare "global" followed by whitespace/end starts a directive.
// Ownership/Lifetime: Test owns constructed module and buffers locally.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"

#include <cassert>
#include <sstream>
#include <string>

int main()
{
    constexpr const char *kProgram = R"(il 0.2.0
global str @greeting = "hello"
func @main() -> void {
global_loop:
  ret
}
)";

    std::istringstream input(kProgram);
    il::core::Module module;
    std::ostringstream diag;

    auto parsed = il::api::v2::parse_text_expected(input, module);
    if (!parsed)
        il::support::printDiag(parsed.error(), diag);

    assert(parsed);
    assert(diag.str().empty());

    assert(module.globals.size() == 1);
    const auto &global = module.globals.front();
    assert(global.name == "greeting");
    assert(global.init == "hello");

    assert(module.functions.size() == 1);
    const auto &fn = module.functions.front();
    assert(fn.blocks.size() == 1);
    const auto &block = fn.blocks.front();
    assert(block.label == "global_loop");

    return 0;
}
