//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_ical_x64.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"
#include "codegen/x86_64/passes/LoweringPass.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "gtest/gtest.h"

// Ensure call.indirect lowers to an indirect CALL in MIR plan.
TEST(Codegen_X64_InterfaceCall, IndirectCallLowering)
{
    il::core::Module m;
    il::build::IRBuilder b(m);
    auto &fn = b.startFunction("f", il::core::Type(il::core::Type::Kind::I64), {});
    b.addBlock(fn, "entry");
    // call.indirect @callee, 0
    il::core::Instr ci{};
    ci.op = il::core::Opcode::CallIndirect;
    ci.type = il::core::Type(il::core::Type::Kind::Void);
    ci.operands.push_back(il::core::Value::global("callee"));
    ci.operands.push_back(il::core::Value::constInt(0));
    fn.blocks.front().instructions.push_back(ci);
    il::core::Instr ret{};
    ret.op = il::core::Opcode::Ret;
    ret.type = il::core::Type(il::core::Type::Kind::Void);
    fn.blocks.front().instructions.push_back(ret);

    viper::codegen::x64::CodegenOptions opts{};
    auto res = viper::codegen::x64::emitFunctionToAssembly(
        viper::codegen::x64::convertToAdapterFunction(fn), opts);
    ASSERT_TRUE(res.ok);
    // Simple smoke: assembly contains "call" mnemonic (indirect form depends on register
    // allocation)
    ASSERT_NE(res.asmText.find("call"), std::string::npos);
}
