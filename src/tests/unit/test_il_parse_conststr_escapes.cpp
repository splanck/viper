//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_conststr_escapes.cpp
// Purpose: Ensure const_str operands decode escape sequences when parsed. 
// Key invariants: Operand parser stores decoded bytes for Value::ConstStr operands.
// Ownership/Lifetime: Test owns the module produced by the parser.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include "support/diag_expected.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

int main()
{
    const char *source = R"(il 0.1.2
func @main() -> void {
entry:
  %s0 = const_str "line\n_tab\t_quote:\"_hex:\x21"
  ret
}
)";

    std::istringstream in(source);
    il::core::Module module;
    auto parsed = il::api::v2::parse_text_expected(in, module);
    if (!parsed)
    {
        il::support::printDiag(parsed.error(), std::cerr);
        return 1;
    }

    assert(module.functions.size() == 1);
    const auto &fn = module.functions[0];
    assert(fn.blocks.size() == 1);

    const auto &block = fn.blocks[0];
    assert(block.instructions.size() == 2);

    const auto &constStr = block.instructions[0];
    assert(constStr.op == il::core::Opcode::ConstStr);
    assert(constStr.operands.size() == 1);
    assert(constStr.operands[0].kind == il::core::Value::Kind::ConstStr);

    const std::string expected = std::string("line\n_tab\t_quote:\"_hex:!");
    assert(constStr.operands[0].str == expected);

    return 0;
}
