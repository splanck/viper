//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_emit_aarch64_minimal.cpp
// Purpose: Verify minimal AArch64 AsmEmitter prologue/epilogue and ADD emission. 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tests/unit/GTestStub.hpp"

#include <sstream>
#include <string>

#include "codegen/aarch64/AsmEmitter.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"

using namespace viper::codegen::aarch64;

TEST(AArch64Emit, PrologueAddEpilogue)
{
    auto &ti = darwinTarget();
    AsmEmitter emit{ti};

    std::ostringstream os;
    const std::string fname = "add_two";
    emit.emitFunctionHeader(os, fname);
    emit.emitPrologue(os);
    // Compute x0 = x0 + x1 and return
    emit.emitAddRRR(os, PhysReg::X0, PhysReg::X0, PhysReg::X1);
    emit.emitEpilogue(os);

    const std::string asmText = os.str();
    // Header directives present
    EXPECT_NE(asmText.find(".text"), std::string::npos);
    EXPECT_NE(asmText.find(".globl " + fname), std::string::npos);
    EXPECT_NE(asmText.find(fname + ":\n"), std::string::npos);
    // Prologue / body / epilogue mnemonics present in the right order
    auto stpPos = asmText.find("stp x29, x30, [sp, #-16]!");
    auto movPos = asmText.find("mov x29, sp");
    auto addPos = asmText.find("add x0, x0, x1");
    auto ldpPos = asmText.find("ldp x29, x30, [sp], #16");
    auto retPos = asmText.find("ret\n");
    ASSERT_NE(stpPos, std::string::npos);
    ASSERT_NE(movPos, std::string::npos);
    ASSERT_NE(addPos, std::string::npos);
    ASSERT_NE(ldpPos, std::string::npos);
    ASSERT_NE(retPos, std::string::npos);
    EXPECT_TRUE(stpPos < movPos);
    EXPECT_TRUE(movPos < addPos);
    EXPECT_TRUE(addPos < ldpPos);
    EXPECT_TRUE(ldpPos < retPos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
