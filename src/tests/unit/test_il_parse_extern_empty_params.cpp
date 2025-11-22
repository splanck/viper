//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_extern_empty_params.cpp
// Purpose: Ensure extern declarations allow empty parameter lists with whitespace. 
// Key invariants: Parser tolerates whitespace-only parameter slices without emitting errors.
// Ownership/Lifetime: Test owns IL module and buffers.
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
#include <string>

int main()
{
    const std::string source = R"(il 0.1.2

extern @noop(   ) -> void

func @main() -> void {
entry:
  call @noop()
  ret
})";

    std::istringstream in(source);
    il::core::Module module;
    auto parsed = il::api::v2::parse_text_expected(in, module);
    assert(parsed);

    assert(module.externs.size() == 1);
    const auto &ext = module.externs.front();
    assert(ext.name == "noop");
    assert(ext.params.empty());
    assert(ext.retType.kind == il::core::Type::Kind::Void);

    assert(module.functions.size() == 1);
    const auto &fn = module.functions.front();
    assert(fn.blocks.size() == 1);
    const auto &entry = fn.blocks.front();
    assert(entry.instructions.size() == 2);

    const auto &callInstr = entry.instructions[0];
    assert(callInstr.op == il::core::Opcode::Call);
    assert(callInstr.callee == "noop");
    assert(callInstr.operands.empty());
    assert(callInstr.type.kind == il::core::Type::Kind::Void);

    const auto &retInstr = entry.instructions[1];
    assert(retInstr.op == il::core::Opcode::Ret);
    assert(retInstr.operands.empty());
    assert(retInstr.type.kind == il::core::Type::Kind::Void);

    auto verifyResult = il::api::v2::verify_module_expected(module);
    assert(verifyResult);

    return 0;
}
