//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_emit_aarch64_frameplan.cpp
// Purpose: Verify AsmEmitter emits additional callee-saved saves/restores per FramePlan. 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tests/unit/GTestStub.hpp"

#include <sstream>
#include <string>

#include "codegen/aarch64/AsmEmitter.hpp"
#include "codegen/aarch64/FramePlan.hpp"

using namespace viper::codegen::aarch64;

TEST(AArch64Emit, FramePlanSavesGPRs)
{
    auto &ti = darwinTarget();
    AsmEmitter emit{ti};
    FramePlan plan{};
    plan.saveGPRs = {PhysReg::X19, PhysReg::X20, PhysReg::X21}; // odd count

    std::ostringstream os;
    emit.emitFunctionHeader(os, "f");
    emit.emitPrologue(os, plan);
    emit.emitEpilogue(os, plan);

    const std::string text = os.str();
    // Expect FP/LR save
    EXPECT_NE(text.find("stp x29, x30, [sp, #-16]!"), std::string::npos);
    // Expect saves: stp x19, x20 and str x21
    EXPECT_NE(text.find("stp x19, x20, [sp, #-16]!"), std::string::npos);
    EXPECT_NE(text.find("str x21, [sp, #-16]!"), std::string::npos);
    // Restores: ldr x21 then ldp x19, x20 then ldp x29, x30
    EXPECT_NE(text.find("ldr x21, [sp], #16"), std::string::npos);
    EXPECT_NE(text.find("ldp x19, x20, [sp], #16"), std::string::npos);
    EXPECT_NE(text.find("ldp x29, x30, [sp], #16"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
