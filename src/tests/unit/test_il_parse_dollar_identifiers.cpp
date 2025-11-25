//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_dollar_identifiers.cpp
// Purpose: Ensure IL parser accepts identifiers containing '$' characters.
// Key invariants: Operand parser should mirror Cursor identifier rules.
// Ownership/Lifetime: Test owns parsed module and diagnostics stream.
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

extern @callee$helper(ptr) -> void

func @main$entry() -> void {
entry$start:
  %tmp$0 = const_str "ok"
  call @callee$helper(%tmp$0)
  br exit$block
exit$block:
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

    assert(module.globals.empty());
    assert(module.externs.size() == 1);
    assert(module.externs[0].name == "callee$helper");
    assert(module.functions.size() == 1);

    const auto &fn = module.functions[0];
    assert(fn.name == "main$entry");
    assert(fn.blocks.size() == 2);
    assert(fn.blocks[0].label == "entry$start");
    assert(fn.blocks[1].label == "exit$block");

    const auto &entry = fn.blocks[0];
    assert(entry.instructions.size() == 3);

    const auto &constStr = entry.instructions[0];
    assert(constStr.op == il::core::Opcode::ConstStr);
    assert(constStr.result.has_value());
    const unsigned resultId = *constStr.result;
    assert(resultId < fn.valueNames.size());
    assert(fn.valueNames[resultId] == "tmp$0");
    assert(constStr.operands.size() == 1);
    assert(constStr.operands[0].kind == il::core::Value::Kind::ConstStr);
    assert(constStr.operands[0].str == "ok");

    const auto &callInstr = entry.instructions[1];
    assert(callInstr.op == il::core::Opcode::Call);
    assert(callInstr.callee == "callee$helper");
    assert(callInstr.operands.size() == 1);
    assert(callInstr.operands[0].kind == il::core::Value::Kind::Temp);
    assert(callInstr.operands[0].id == resultId);

    const auto &brInstr = entry.instructions[2];
    assert(brInstr.op == il::core::Opcode::Br);
    assert(brInstr.labels.size() == 1);
    assert(brInstr.labels[0] == "exit$block");

    return 0;
}
