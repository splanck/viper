//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/vm/test_ical_vm.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "vm/VM.hpp"
#include "gtest/gtest.h"

// Minimal smoke: ensure call.indirect with function pointer executes.
TEST(VM_InterfaceCall, IndirectFunctionPointerExecutes)
{
    il::core::Module m;
    il::build::IRBuilder b(m);

    // callee: func @callee(ptr) -> void { entry: ret }
    auto &callee = b.startFunction("callee",
                                   il::core::Type(il::core::Type::Kind::Void),
                                   std::vector<il::core::Param>{il::core::Param{
                                       "ME", il::core::Type(il::core::Type::Kind::Ptr)}});
    b.addBlock(callee, "entry");
    b.emitRet(std::nullopt, {});

    // main: take &@callee as a global label, call.indirect
    auto &mainFn = b.startFunction("main", il::core::Type(il::core::Type::Kind::I64), {});
    b.addBlock(mainFn, "entry");
    // call.indirect %callee_label, null
    il::core::Instr ci{};
    ci.op = il::core::Opcode::CallIndirect;
    ci.type = il::core::Type(il::core::Type::Kind::Void);
    ci.operands.push_back(il::core::Value::global("callee"));
    ci.operands.push_back(il::core::Value::null());
    mainFn.blocks.front().instructions.push_back(ci);
    il::core::Instr ret{};
    ret.op = il::core::Opcode::Ret;
    ret.type = il::core::Type(il::core::Type::Kind::Void);
    mainFn.blocks.front().instructions.push_back(ret);

    il::vm::VM vm(m);
    auto result = vm.runFunction("main");
    (void)result;
    SUCCEED();
}
