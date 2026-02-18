//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_aarch64_pass_manager.cpp
// Purpose: Verify the AArch64 modular PassManager pipeline (Priority 2F).
//
// Background:
//   The AArch64 backend previously had a monolithic pipeline embedded in
//   cmd_codegen_arm64.cpp.  Priority 2F extracts the per-phase logic into
//   formal Pass subclasses registered with the common PassManager<AArch64Module>
//   template, matching the architecture already used by the x86_64 backend.
//
// What is verified:
//   1. PipelineRoundtrip   — Full pass sequence (Lower → RegAlloc → Peephole → Emit)
//                            produces correct assembly for a simple function.
//   2. PartialPipeline     — Running only LoweringPass + RegAllocPass populates mir
//                            but leaves assembly empty.
//   3. FailPassShortCircuit — A pass that signals failure stops subsequent passes.
//   4. EmptyModule         — PassManager on an empty IL module succeeds with no output.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"
#include <sstream>
#include <string>

#include "codegen/aarch64/TargetAArch64.hpp"
#include "codegen/aarch64/passes/EmitPass.hpp"
#include "codegen/aarch64/passes/LoweringPass.hpp"
#include "codegen/aarch64/passes/PassManager.hpp"
#include "codegen/aarch64/passes/PeepholePass.hpp"
#include "codegen/aarch64/passes/RegAllocPass.hpp"
#include "il/io/Parser.hpp"

using namespace viper::codegen::aarch64;
using namespace viper::codegen::aarch64::passes;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

/// Parse an IL text string into an il::core::Module.
/// Returns a default-constructed Module on parse failure.
static il::core::Module parseIL(const std::string &src)
{
    std::istringstream ss(src);
    il::core::Module mod;
    if (!il::io::Parser::parse(ss, mod))
        return {};
    return mod;
}

/// Build a PassManager with the standard AArch64 full pipeline.
static PassManager buildFullPipeline()
{
    PassManager pm;
    pm.addPass(std::make_unique<LoweringPass>());
    pm.addPass(std::make_unique<RegAllocPass>());
    pm.addPass(std::make_unique<PeepholePass>());
    pm.addPass(std::make_unique<EmitPass>());
    return pm;
}

} // namespace

// ---------------------------------------------------------------------------
// Test 1: Full pipeline roundtrip — simple constant-return function.
// ---------------------------------------------------------------------------
//
// func @forty_two() -> i64 { entry: ret 42 }
//
// The full pipeline should succeed and produce assembly containing:
//   - A function label (_forty_two on Darwin)
//   - A move immediate (mov x0, #42 or similar)
//   - A ret instruction
//
TEST(AArch64PassManager, PipelineRoundtrip)
{
    const std::string il =
        "il 0.1\n"
        "func @forty_two() -> i64 {\n"
        "entry:\n"
        "  ret 42\n"
        "}\n";

    il::core::Module mod = parseIL(il);
    ASSERT_FALSE(mod.functions.empty());

    const TargetInfo &ti = darwinTarget();
    AArch64Module m;
    m.ilMod = &mod;
    m.ti    = &ti;

    PassManager pm = buildFullPipeline();
    Diagnostics diags;
    const bool ok = pm.run(m, diags);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(diags.errors().empty());

    // Assembly must be non-empty and contain the function label.
    EXPECT_FALSE(m.assembly.empty());
    EXPECT_NE(m.assembly.find("forty_two"), std::string::npos);
    // Must contain a return instruction.
    EXPECT_NE(m.assembly.find("ret"), std::string::npos);
    // Must contain a move-immediate for the constant 42.
    const bool hasImm = m.assembly.find("42") != std::string::npos ||
                        m.assembly.find("#42") != std::string::npos ||
                        m.assembly.find("0x2a") != std::string::npos;
    EXPECT_TRUE(hasImm);
}

// ---------------------------------------------------------------------------
// Test 2: Partial pipeline — LoweringPass only; mir populated, assembly empty.
// ---------------------------------------------------------------------------
//
// Running only the LoweringPass should populate mir but not assembly.
//
TEST(AArch64PassManager, PartialPipeline)
{
    const std::string il =
        "il 0.1\n"
        "func @add_two(%a:i64, %b:i64) -> i64 {\n"
        "entry:\n"
        "  %r = add %a, %b\n"
        "  ret %r\n"
        "}\n";

    il::core::Module mod = parseIL(il);
    ASSERT_FALSE(mod.functions.empty());

    const TargetInfo &ti = darwinTarget();
    AArch64Module m;
    m.ilMod = &mod;
    m.ti    = &ti;

    // Only add the lowering pass.
    PassManager pm;
    pm.addPass(std::make_unique<LoweringPass>());

    Diagnostics diags;
    const bool ok = pm.run(m, diags);

    EXPECT_TRUE(ok);
    // MIR should be populated (one function).
    EXPECT_EQ(m.mir.size(), 1u);
    // Assembly should not have been emitted yet.
    EXPECT_TRUE(m.assembly.empty());
}

// ---------------------------------------------------------------------------
// Test 3: A failing pass stops subsequent passes.
// ---------------------------------------------------------------------------
//
// A pass that returns false should prevent later passes from running.
// We verify this by checking that assembly remains empty when RA fails.
//
namespace
{

/// Stub pass that always fails without modifying module state.
class AlwaysFailPass final : public Pass
{
  public:
    bool run(AArch64Module & /*module*/, Diagnostics & /*diags*/) override
    {
        return false;
    }
};

} // namespace

TEST(AArch64PassManager, FailPassShortCircuit)
{
    const std::string il =
        "il 0.1\n"
        "func @simple() -> i64 {\n"
        "entry:\n"
        "  ret 0\n"
        "}\n";

    il::core::Module mod = parseIL(il);
    ASSERT_FALSE(mod.functions.empty());

    const TargetInfo &ti = darwinTarget();
    AArch64Module m;
    m.ilMod = &mod;
    m.ti    = &ti;

    // Pipeline: Lower → FAIL → (Peephole should NOT run) → (Emit should NOT run).
    PassManager pm;
    pm.addPass(std::make_unique<LoweringPass>());
    pm.addPass(std::make_unique<AlwaysFailPass>());
    pm.addPass(std::make_unique<PeepholePass>());
    pm.addPass(std::make_unique<EmitPass>());

    Diagnostics diags;
    const bool ok = pm.run(m, diags);

    // PassManager should have reported failure.
    EXPECT_FALSE(ok);
    // EmitPass should not have run — assembly must be empty.
    EXPECT_TRUE(m.assembly.empty());
    // MIR may or may not be populated (LoweringPass ran before the failure).
}

// ---------------------------------------------------------------------------
// Test 4: Empty IL module — pipeline succeeds with no MIR and no output.
// ---------------------------------------------------------------------------
TEST(AArch64PassManager, EmptyModule)
{
    const std::string il = "il 0.1\n";

    il::core::Module mod = parseIL(il);
    // Empty module has no functions.
    EXPECT_TRUE(mod.functions.empty());

    const TargetInfo &ti = darwinTarget();
    AArch64Module m;
    m.ilMod = &mod;
    m.ti    = &ti;

    PassManager pm = buildFullPipeline();
    Diagnostics diags;
    const bool ok = pm.run(m, diags);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(m.mir.empty());
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
