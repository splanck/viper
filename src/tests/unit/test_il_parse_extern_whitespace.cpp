//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_extern_whitespace.cpp
// Purpose: Ensure extern declarations tolerate incidental whitespace around names.
// Key invariants: Parser trims extern identifiers so verification resolves calls.
// Ownership/Lifetime: Test owns IL module and buffers.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <cassert>
#include <fstream>
#include <sstream>

#ifndef PARSE_ROUNDTRIP_DIR
#error "PARSE_ROUNDTRIP_DIR must be defined"
#endif

int main()
{
    const char *path = PARSE_ROUNDTRIP_DIR "/extern_whitespace.il";
    std::ifstream input(path);
    assert(input && "failed to open extern_whitespace.il");

    std::stringstream buffer;
    buffer << input.rdbuf();
    buffer.seekg(0);

    il::core::Module module;
    auto parseResult = il::api::v2::parse_text_expected(buffer, module);
    assert(parseResult);

    assert(module.externs.size() == 1);
    const auto &ext = module.externs.front();
    assert(ext.name == "foo");
    assert(ext.params.size() == 2);
    assert(ext.params[0].kind == il::core::Type::Kind::I64);
    assert(ext.params[1].kind == il::core::Type::Kind::I64);
    assert(ext.retType.kind == il::core::Type::Kind::Void);

    assert(module.functions.size() == 1);
    const auto &fn = module.functions.front();
    assert(fn.blocks.size() == 1);
    const auto &entry = fn.blocks.front();
    assert(entry.instructions.size() == 2);

    const auto &callInstr = entry.instructions[0];
    assert(callInstr.op == il::core::Opcode::Call);
    assert(callInstr.callee == "foo");
    assert(callInstr.operands.size() == 2);
    assert(callInstr.operands[0].kind == il::core::Value::Kind::ConstInt);
    assert(callInstr.operands[1].kind == il::core::Value::Kind::ConstInt);
    assert(callInstr.type.kind == il::core::Type::Kind::Void);

    const auto &retInstr = entry.instructions[1];
    assert(retInstr.op == il::core::Opcode::Ret);
    assert(retInstr.operands.empty());
    assert(retInstr.type.kind == il::core::Type::Kind::Void);

    auto verifyResult = il::api::v2::verify_module_expected(module);
    assert(verifyResult);

    return 0;
}
