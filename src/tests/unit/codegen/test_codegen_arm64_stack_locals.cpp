//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_stack_locals.cpp
// Purpose: Verify AArch64 codegen for stack-allocated locals (alloca/load/store).
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tests/unit/GTestStub.hpp"

#include <sstream>
#include <string>

#include "codegen/aarch64/AsmEmitter.hpp"
#include "codegen/aarch64/LowerILToMIR.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

using namespace viper::codegen::aarch64;
using namespace il;

/// @brief Returns the expected mangled symbol name on Darwin.
static std::string mangledSym(const std::string &name)
{
#if defined(__APPLE__)
    return "_" + name;
#else
    return name;
#endif
}

/// Build a simple IL function that allocates a local, stores param0 to it, loads it back, and
/// returns it.
/// define i64 @test_local(i64 %0) {
/// entry:
///   %1 = alloca i64
///   store i64 %0, i64* %1
///   %2 = load i64, i64* %1
///   ret i64 %2
/// }
static il::core::Function buildTestFunction()
{
    il::core::Function fn;
    fn.name = "test_local";
    fn.retType = core::Type(core::Type::Kind::I64);

    core::BasicBlock entry;
    entry.label = "entry";

    // Add parameter %0
    core::Param param;
    param.id = 0;
    param.type = core::Type(core::Type::Kind::I64);
    entry.params.push_back(param);

    // %1 = alloca i64
    core::Instr allocaInstr;
    allocaInstr.result = 1;
    allocaInstr.op = core::Opcode::Alloca;
    allocaInstr.type = core::Type(core::Type::Kind::Ptr);
    allocaInstr.operands.push_back(core::Value::constInt(8)); // 8 bytes for i64
    allocaInstr.loc = {1, 1, 0};
    entry.instructions.push_back(allocaInstr);

    // store i64, %1, %0 (pointer, value)
    core::Instr storeInstr;
    storeInstr.op = core::Opcode::Store;
    storeInstr.type = core::Type(core::Type::Kind::I64);
    storeInstr.operands.push_back(core::Value::temp(1)); // pointer (%1 = alloca result)
    storeInstr.operands.push_back(core::Value::temp(0)); // value to store (param %0)
    storeInstr.loc = {2, 1, 0};
    entry.instructions.push_back(storeInstr);

    // %2 = load i64, i64* %1
    core::Instr loadInstr;
    loadInstr.result = 2;
    loadInstr.op = core::Opcode::Load;
    loadInstr.type = core::Type(core::Type::Kind::I64);
    loadInstr.operands.push_back(core::Value::temp(1)); // pointer to load from
    loadInstr.loc = {3, 1, 0};
    entry.instructions.push_back(loadInstr);

    // ret i64 %2
    core::Instr retInstr;
    retInstr.op = core::Opcode::Ret;
    retInstr.type = core::Type(core::Type::Kind::Void);
    retInstr.operands.push_back(core::Value::temp(2)); // return loaded value
    retInstr.loc = {4, 1, 0};
    entry.instructions.push_back(retInstr);

    fn.blocks.push_back(entry);
    return fn;
}

TEST(AArch64Codegen, StackLocals_AllocaLoadStore)
{
    auto &ti = darwinTarget();
    LowerILToMIR lowerer{ti};
    AsmEmitter emitter{ti};

    // Build IL function
    il::core::Function fn = buildTestFunction();

    // Lower to MIR
    MFunction mfn = lowerer.lowerFunction(fn);

    // Check that local frame size is set
    EXPECT_TRUE(mfn.localFrameSize > 0);
    EXPECT_EQ(mfn.localFrameSize % 16, 0); // Should be 16-byte aligned

    // Emit assembly
    std::ostringstream os;
    emitter.emitFunction(os, mfn);
    const std::string asmText = os.str();

    // Verify function header and prologue
    EXPECT_NE(asmText.find(".text"), std::string::npos);
    EXPECT_NE(asmText.find(".globl " + mangledSym("test_local")), std::string::npos);
    EXPECT_NE(asmText.find(mangledSym("test_local") + ":"), std::string::npos);
    EXPECT_NE(asmText.find("stp x29, x30"), std::string::npos);
    EXPECT_NE(asmText.find("mov x29, sp"), std::string::npos);

    // Verify stack allocation for locals
    EXPECT_NE(asmText.find("sub sp, sp, #"), std::string::npos);

    // Verify store to stack (str x0, [x29, #offset])
    EXPECT_NE(asmText.find("str x0, [x29, #"), std::string::npos);

    // Verify load from stack (ldr x0, [x29, #offset])
    EXPECT_NE(asmText.find("ldr x0, [x29, #"), std::string::npos);

    // Verify stack deallocation and epilogue
    EXPECT_NE(asmText.find("add sp, sp, #"), std::string::npos);
    EXPECT_NE(asmText.find("ldp x29, x30"), std::string::npos);
    EXPECT_NE(asmText.find("ret"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
