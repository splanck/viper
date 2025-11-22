// File: tests/unit/codegen/test_regalloc_aarch64_linear.cpp
// Purpose: Validate AArch64 linear-scan allocator assigns phys regs, spills,
// //        and records callee-saved usage on pressure.

#include "tests/unit/GTestStub.hpp"

#include <sstream>
#include <string>

#include "codegen/aarch64/AsmEmitter.hpp"
#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/FrameBuilder.hpp"
#include "codegen/aarch64/RegAllocLinear.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"

using namespace viper::codegen::aarch64;

TEST(Arm64RegAlloc, SpillsAndCalleeSaved)
{
    auto &ti = darwinTarget();
    MFunction fn{};
    fn.name = "ra_test";
    fn.blocks.push_back(MBasicBlock{});
    auto &bb = fn.blocks.back();
    bb.name = "entry";

    // Reserve a local alloca (one i64)
    FrameBuilder fb{fn};
    fb.addLocal(/*tempId*/ 1, /*size*/ 8, /*align*/ 8);

    // Create many virtual temporaries to exceed the available caller-saved pool
    // and force usage of callee-saved + spills. We define 40 vregs sequentially.
    const int N = 40;
    for (int i = 0; i < N; ++i)
    {
        MOperand dst = MOperand::vregOp(RegClass::GPR, static_cast<uint16_t>(i));
        bb.instrs.push_back(MInstr{MOpcode::MovRI, {dst, MOperand::immOp(i)}});
    }
    // Use a couple of early vregs to trigger reloads
    MOperand use0 = MOperand::vregOp(RegClass::GPR, 0);
    MOperand use1 = MOperand::vregOp(RegClass::GPR, 1);
    MOperand dst = MOperand::vregOp(RegClass::GPR, static_cast<uint16_t>(N));
    bb.instrs.push_back(MInstr{MOpcode::AddRRR, {dst, use0, use1}});
    // Move result to x0 to make output deterministic
    bb.instrs.push_back(MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), dst}});

    // Run allocator
    auto result = allocate(fn, ti);
    // Expect that we used at least one callee-saved register
    bool usedCallee = false;
    for (auto r : fn.savedGPRs)
    {
        if (std::find(ti.calleeSavedGPR.begin(), ti.calleeSavedGPR.end(), r) !=
            ti.calleeSavedGPR.end())
        {
            usedCallee = true;
        }
    }
    EXPECT_TRUE(usedCallee);

    // Emit to text and look for spills/loads and prologue shape
    AsmEmitter emit{ti};
    std::ostringstream os;
    emit.emitFunction(os, fn);
    const std::string asmText = os.str();
    // Spill stores and reload loads should appear
    EXPECT_NE(asmText.find("str x"), std::string::npos);
    EXPECT_NE(asmText.find("ldr x"), std::string::npos);

    // Prologue must adjust sp by total frame size
    const int frameSize = fn.frame.totalBytes;
    if (frameSize > 0)
    {
        const std::string sub = "sub sp, sp, #" + std::to_string(frameSize);
        EXPECT_NE(asmText.find(sub), std::string::npos);
    }
    // Callee-saved used in RA must be saved in prologue
    for (auto r : fn.savedGPRs)
    {
        const char *name = regName(r);
        // look for either stp/str with the name
        const std::string needle1 = std::string("stp ") + name;
        const std::string needle2 = std::string("str ") + name;
        EXPECT_TRUE(asmText.find(needle1) != std::string::npos ||
                    asmText.find(needle2) != std::string::npos);
    }
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
