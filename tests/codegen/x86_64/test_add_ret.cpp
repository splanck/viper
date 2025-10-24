// File: tests/codegen/x86_64/test_add_ret.cpp
// Purpose: Ensure the x86-64 backend emits basic add/return assembly for the
//          temporary IL adapter module.
// Key invariants: Backend output must contain the function symbol directive and
//                 core instruction sequence for mov/add/ret.
// Ownership/Lifetime: Test constructs the IL adapter structures by value and
//                     discards them after emission.
// Links: src/codegen/x86_64/Backend.hpp, src/codegen/x86_64/LowerILToMIR.hpp

#include <string>

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#else
#include "../../unit/GTestStub.hpp"
#endif

#include "codegen/x86_64/Backend.hpp"

namespace viper::codegen::x64
{
namespace
{
[[nodiscard]] TargetInfo makeSysVTargetForTest()
{
    TargetInfo info{};
    info.callerSavedGPR = {
        PhysReg::RSI,
        PhysReg::RDI,
        PhysReg::RAX,
    };
    info.calleeSavedGPR = {};
    info.callerSavedXMM = {
        PhysReg::XMM0,
        PhysReg::XMM1,
        PhysReg::XMM2,
        PhysReg::XMM3,
        PhysReg::XMM4,
        PhysReg::XMM5,
        PhysReg::XMM6,
        PhysReg::XMM7,
        PhysReg::XMM8,
        PhysReg::XMM9,
        PhysReg::XMM10,
        PhysReg::XMM11,
        PhysReg::XMM12,
        PhysReg::XMM13,
        PhysReg::XMM14,
        PhysReg::XMM15,
    };
    info.calleeSavedXMM = {};
    info.intArgOrder = {
        PhysReg::RDI,
        PhysReg::RSI,
        PhysReg::RAX,
        PhysReg::RAX,
        PhysReg::RAX,
        PhysReg::RAX,
    };
    info.f64ArgOrder = {
        PhysReg::XMM0,
        PhysReg::XMM1,
        PhysReg::XMM2,
        PhysReg::XMM3,
        PhysReg::XMM4,
        PhysReg::XMM5,
        PhysReg::XMM6,
        PhysReg::XMM7,
    };
    info.intReturnReg = PhysReg::RAX;
    info.f64ReturnReg = PhysReg::XMM0;
    info.stackAlignment = 16U;
    info.hasRedZone = true;
    return info;
}

TargetInfo sysvTargetInstance = makeSysVTargetForTest();
} // namespace

constexpr TargetInfo &sysvTarget() noexcept
{
    return sysvTargetInstance;
}
} // namespace viper::codegen::x64

#include "codegen/x86_64/LowerILToMIR.cpp"
#include "codegen/x86_64/Backend.cpp"

namespace
{
using namespace viper::codegen::x64;

[[nodiscard]] ILModule makeAddModule()
{
    ILValue argA{};
    argA.kind = ILValue::Kind::I64;
    argA.id = 0;

    ILValue argB{};
    argB.kind = ILValue::Kind::I64;
    argB.id = 1;

    ILInstr addInstr{};
    addInstr.opcode = "add";
    addInstr.resultId = 2;
    addInstr.resultKind = ILValue::Kind::I64;
    addInstr.ops.push_back(argB);
    addInstr.ops.push_back(argA);

    ILValue sum{};
    sum.kind = ILValue::Kind::I64;
    sum.id = addInstr.resultId;

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    retInstr.ops.push_back(sum);

    ILBlock entry{};
    entry.name = "add";
    entry.paramIds = {argB.id, argA.id};
    entry.paramKinds = {argB.kind, argA.kind};
    entry.instrs.push_back(addInstr);
    entry.instrs.push_back(retInstr);

    ILFunction function{};
    function.name = "add";
    function.blocks.push_back(entry);

    ILModule module{};
    module.funcs.push_back(function);
    return module;
}
} // namespace

TEST(X86_64BackendCodegenTest, EmitsAddAssemblySequence)
{
    const ILModule module = makeAddModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    ASSERT_TRUE(result.errors.empty());
    ASSERT_FALSE(result.asmText.empty());

    EXPECT_NE(result.asmText.find(".globl add"), std::string::npos);
    EXPECT_NE(result.asmText.find("movq %rdi, %rax"), std::string::npos);
    EXPECT_NE(result.asmText.find("addq %rsi, %rax"), std::string::npos);
    EXPECT_NE(result.asmText.find("  ret"), std::string::npos);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
