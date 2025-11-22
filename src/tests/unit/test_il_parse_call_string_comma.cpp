//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_call_string_comma.cpp
// Purpose: Ensure call operand parsing preserves commas inside string literals. 
// Key invariants: Parser keeps string arguments intact even when containing delimiters.
// Ownership/Lifetime: Test constructs modules and parser inputs locally.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"

#include <cassert>
#include <sstream>

int main()
{
    const char *src = R"VIPER(il 0.1.2
extern @print(str) -> void
func @main() -> void {
entry:
  call @print("hello, world")
  call @print("value)")
  br label ^dest("error, detail")
dest(%msg:str):
  ret
}
)VIPER";

    std::istringstream in(src);
    il::core::Module module;
    auto parse = il::api::v2::parse_text_expected(in, module);
    assert(parse);

    assert(module.functions.size() == 1);
    const auto &fn = module.functions[0];
    assert(fn.blocks.size() == 2);

    const auto &entry = fn.blocks[0];
    assert(entry.instructions.size() == 3);

    const auto &callInstr = entry.instructions[0];
    assert(callInstr.op == il::core::Opcode::Call);
    assert(callInstr.callee == "print");
    assert(callInstr.operands.size() == 1);
    assert(callInstr.operands[0].kind == il::core::Value::Kind::ConstStr);
    assert(callInstr.operands[0].str == "hello, world");
    assert(callInstr.type.kind == il::core::Type::Kind::Void);

    const auto &callWithParen = entry.instructions[1];
    assert(callWithParen.op == il::core::Opcode::Call);
    assert(callWithParen.callee == "print");
    assert(callWithParen.operands.size() == 1);
    assert(callWithParen.operands[0].kind == il::core::Value::Kind::ConstStr);
    assert(callWithParen.operands[0].str == "value)");
    assert(callWithParen.type.kind == il::core::Type::Kind::Void);

    const auto &branchInstr = entry.instructions[2];
    assert(branchInstr.op == il::core::Opcode::Br);
    assert(branchInstr.labels.size() == 1);
    assert(branchInstr.labels[0] == "dest");
    assert(branchInstr.brArgs.size() == 1);
    assert(branchInstr.brArgs[0].size() == 1);
    assert(branchInstr.brArgs[0][0].kind == il::core::Value::Kind::ConstStr);
    assert(branchInstr.brArgs[0][0].str == "error, detail");

    const auto &destBlock = fn.blocks[1];
    assert(destBlock.label == "dest");
    assert(destBlock.params.size() == 1);
    assert(destBlock.params[0].name == "msg");
    assert(destBlock.params[0].type.kind == il::core::Type::Kind::Str);
    assert(destBlock.instructions.size() == 1);
    assert(destBlock.instructions[0].op == il::core::Opcode::Ret);

    return 0;
}
