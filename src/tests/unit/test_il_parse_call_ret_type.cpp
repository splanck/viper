//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_call_ret_type.cpp
// Purpose: Ensure parsing a call with a non-void return preserves the instruction type.
// Key invariants: Call instruction retains deduced result type from annotation/signature.
// Ownership/Lifetime: Test owns module and input stream locally.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"

#include <cassert>
#include <sstream>

int main()
{
    constexpr const char *kProgram = R"(il 0.1.2

extern @foo() -> str

func @main() -> void {
entry:
  %s: str = call @foo()
  ret
}
)";

    std::istringstream input(kProgram);
    il::core::Module module;
    auto parse = il::api::v2::parse_text_expected(input, module);
    assert(parse);

    assert(module.functions.size() == 1);
    const auto &fn = module.functions.front();
    assert(fn.blocks.size() == 1);
    const auto &entry = fn.blocks.front();
    assert(entry.instructions.size() == 2);

    const auto &callInstr = entry.instructions[0];
    assert(callInstr.op == il::core::Opcode::Call);
    assert(callInstr.result.has_value());
    assert(callInstr.type.kind == il::core::Type::Kind::Str);

    const auto &retInstr = entry.instructions[1];
    assert(retInstr.op == il::core::Opcode::Ret);
    assert(retInstr.type.kind == il::core::Type::Kind::Void);

    auto verify = il::api::v2::verify_module_expected(module);
    assert(verify);

    return 0;
}
