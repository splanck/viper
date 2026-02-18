//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_aarch64_block_layout.cpp
// Purpose: Verify the AArch64 block layout pass (Priority 3J).
//
// Background:
//   IL blocks are lowered to MIR in definition order. When a function's IL
//   defines blocks in an order that differs from the optimal execution order
//   (e.g., an early-exit block placed before the loop body), the resulting
//   assembly contains extra unconditional branches that the peephole cannot
//   eliminate on its own.
//
//   BlockLayoutPass applies a greedy trace algorithm: starting from the
//   entry block, it repeatedly places the target of each unconditional branch
//   (Br) as the immediately following block. After reordering, PeepholePass
//   can eliminate the resulting fall-through branches.
//
//   Key invariant: the pass only reorders MIR blocks; it never adds, removes,
//   or modifies any instruction. Block names and branch targets are stable.
//
// Tests:
//   1. CorrectOutput        — Full pipeline with layout pass produces correct asm.
//   2. BlockCountStable     — Block count unchanged (pure reorder).
//   3. LoopBranchReduced    — Suboptimal block order (exit before loop) is
//                             corrected, reducing unconditional branch count.
//   4. EntryBlockFirst      — Entry block (block 0) always remains first.
//   5. PipelineIntegration  — BlockLayoutPass between RegAlloc and Peephole
//                             integrates cleanly with the full PassManager.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"
#include <sstream>
#include <string>

#include "codegen/aarch64/TargetAArch64.hpp"
#include "codegen/aarch64/passes/BlockLayoutPass.hpp"
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

static il::core::Module parseIL(const std::string &src)
{
    std::istringstream ss(src);
    il::core::Module mod;
    if (!il::io::Parser::parse(ss, mod))
        return {};
    return mod;
}

/// Full pipeline with BlockLayoutPass inserted between RegAlloc and Peephole.
static PassManager buildLayoutPipeline()
{
    PassManager pm;
    pm.addPass(std::make_unique<LoweringPass>());
    pm.addPass(std::make_unique<RegAllocPass>());
    pm.addPass(std::make_unique<BlockLayoutPass>());
    pm.addPass(std::make_unique<PeepholePass>());
    pm.addPass(std::make_unique<EmitPass>());
    return pm;
}

/// Count occurrences of a literal substring.
static int countSubstr(const std::string &text, const std::string &needle)
{
    int n = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos)
    {
        ++n;
        pos += needle.size();
    }
    return n;
}

} // namespace

// ---------------------------------------------------------------------------
// Test 1: Full pipeline with layout pass produces correct assembly.
// ---------------------------------------------------------------------------
TEST(AArch64BlockLayout, CorrectOutput)
{
    const std::string il =
        "il 0.1\n"
        "func @layout_simple() -> i64 {\n"
        "entry:\n"
        "  %a = add 1, 2\n"
        "  %b = add 3, 4\n"
        "  %c = add %a, %b\n"
        "  ret %c\n"
        "}\n";

    il::core::Module mod = parseIL(il);
    ASSERT_FALSE(mod.functions.empty());

    const TargetInfo &ti = darwinTarget();
    AArch64Module m;
    m.ilMod = &mod;
    m.ti    = &ti;

    Diagnostics diags;
    EXPECT_TRUE(buildLayoutPipeline().run(m, diags));
    EXPECT_FALSE(m.assembly.empty());
    EXPECT_NE(m.assembly.find("layout_simple"), std::string::npos);
    EXPECT_NE(m.assembly.find("add"), std::string::npos);
    EXPECT_NE(m.assembly.find("ret"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 2: Block count is unchanged after layout (pure reordering).
// ---------------------------------------------------------------------------
//
// Run both with and without BlockLayoutPass and compare the number of block
// labels in the assembly. The pass must not add or remove any blocks.
//
TEST(AArch64BlockLayout, BlockCountStable)
{
    const std::string il =
        "il 0.1\n"
        "func @block_count() -> i64 {\n"
        "entry:\n"
        "  br loop(0)\n"
        "loop(%i:i64):\n"
        "  %next = add %i, 1\n"
        "  %done = icmp_eq %next, 10\n"
        "  cbr %done, exit(%next), loop(%next)\n"
        "exit(%r:i64):\n"
        "  ret %r\n"
        "}\n";

    // Without BlockLayoutPass.
    PassManager withoutLayout;
    withoutLayout.addPass(std::make_unique<LoweringPass>());
    withoutLayout.addPass(std::make_unique<RegAllocPass>());
    withoutLayout.addPass(std::make_unique<PeepholePass>());
    withoutLayout.addPass(std::make_unique<EmitPass>());

    il::core::Module mod1 = parseIL(il);
    il::core::Module mod2 = parseIL(il);
    ASSERT_FALSE(mod1.functions.empty());
    ASSERT_FALSE(mod2.functions.empty());

    const TargetInfo &ti = darwinTarget();
    AArch64Module m1, m2;
    m1.ilMod = &mod1; m1.ti = &ti;
    m2.ilMod = &mod2; m2.ti = &ti;

    Diagnostics d1, d2;
    ASSERT_TRUE(withoutLayout.run(m1, d1));
    ASSERT_TRUE(buildLayoutPipeline().run(m2, d2));

    // Count block labels (non-prefixed labels ending with ':' not starting with '.').
    auto countLabels = [](const std::string &asm_) {
        int n = 0;
        std::istringstream ss(asm_);
        std::string line;
        while (std::getline(ss, line))
            if (!line.empty() && line.back() == ':' && line[0] != ' ' && line[0] != '\t'
                && line[0] != '.')
                ++n;
        return n;
    };

    const int labelsWithout = countLabels(m1.assembly);
    const int labelsWith    = countLabels(m2.assembly);

    if (labelsWithout != labelsWith)
    {
        std::cerr << "Without layout: " << labelsWithout << " labels\n"
                  << "With layout:    " << labelsWith << " labels\n";
    }
    EXPECT_TRUE(labelsWithout == labelsWith);
}

// ---------------------------------------------------------------------------
// Test 3: Suboptimal block order is corrected — fewer unconditional branches.
// ---------------------------------------------------------------------------
//
// This IL deliberately defines the exit block BEFORE the loop blocks:
//   entry → start → loop → start (back-edge), or start → exit
//
// Block definition order: [entry, exit, start, loop]
//
// Without layout, entry needs an explicit "b start" (forward jump over exit).
// With layout, the trace reorders to [entry, start, loop, exit], and the
// PeepholePass eliminates the now-redundant "b start" from entry.
//
// Measurable: unconditional "  b " count should drop by at least 1.
//
TEST(AArch64BlockLayout, LoopBranchReduced)
{
    // Exit is defined before start/loop — forcing a suboptimal block order.
    const std::string il =
        "il 0.1\n"
        "func @loop_sum() -> i64 {\n"
        "entry:\n"
        "  br start(0, 0)\n"
        "exit(%r:i64):\n"
        "  ret %r\n"
        "start(%i:i64, %s:i64):\n"
        "  %done = icmp_eq %i, 10\n"
        "  cbr %done, exit(%s), loop(%i, %s)\n"
        "loop(%i:i64, %s:i64):\n"
        "  %ns = add %s, %i\n"
        "  %ni = add %i, 1\n"
        "  br start(%ni, %ns)\n"
        "}\n";

    // Without BlockLayoutPass.
    PassManager withoutLayout;
    withoutLayout.addPass(std::make_unique<LoweringPass>());
    withoutLayout.addPass(std::make_unique<RegAllocPass>());
    withoutLayout.addPass(std::make_unique<PeepholePass>());
    withoutLayout.addPass(std::make_unique<EmitPass>());

    il::core::Module mod1 = parseIL(il);
    il::core::Module mod2 = parseIL(il);
    ASSERT_FALSE(mod1.functions.empty());
    ASSERT_FALSE(mod2.functions.empty());

    const TargetInfo &ti = darwinTarget();
    AArch64Module m1, m2;
    m1.ilMod = &mod1; m1.ti = &ti;
    m2.ilMod = &mod2; m2.ti = &ti;

    Diagnostics d1, d2;
    ASSERT_TRUE(withoutLayout.run(m1, d1));
    ASSERT_TRUE(buildLayoutPipeline().run(m2, d2));

    // Count unconditional branch instructions "  b " (not b.cond).
    // We look for lines containing "  b " followed by a non-dot character
    // (to exclude "  b.ne", "  b.eq", etc).
    auto countUnconditionalBranches = [](const std::string &asm_) -> int
    {
        int n = 0;
        std::istringstream ss(asm_);
        std::string line;
        while (std::getline(ss, line))
        {
            // Match "  b <label>" but not "  b.<cond>"
            auto pos = line.find("  b ");
            if (pos != std::string::npos && pos + 4 < line.size()
                && line[pos + 3] == ' ')
                ++n;
        }
        return n;
    };

    const int brWithout = countUnconditionalBranches(m1.assembly);
    const int brWith    = countUnconditionalBranches(m2.assembly);

    if (brWith >= brWithout)
    {
        std::cerr << "Expected fewer unconditional branches with BlockLayoutPass.\n"
                  << "Without: " << brWithout << "\n"
                  << "With:    " << brWith << "\n"
                  << "--- Without layout ---\n" << m1.assembly
                  << "--- With layout ---\n" << m2.assembly;
    }
    EXPECT_TRUE(brWith < brWithout);
}

// ---------------------------------------------------------------------------
// Test 4: Entry block (index 0) always remains first after layout.
// ---------------------------------------------------------------------------
TEST(AArch64BlockLayout, EntryBlockFirst)
{
    const std::string il =
        "il 0.1\n"
        "func @entry_first() -> i64 {\n"
        "entry:\n"
        "  br loop(0)\n"
        "loop(%i:i64):\n"
        "  %next = add %i, 1\n"
        "  %done = icmp_eq %next, 5\n"
        "  cbr %done, exit(%next), loop(%next)\n"
        "exit(%r:i64):\n"
        "  ret %r\n"
        "}\n";

    il::core::Module mod = parseIL(il);
    ASSERT_FALSE(mod.functions.empty());

    const TargetInfo &ti = darwinTarget();
    AArch64Module m;
    m.ilMod = &mod;
    m.ti    = &ti;

    // Run only through BlockLayoutPass (no emit needed).
    PassManager pm;
    pm.addPass(std::make_unique<LoweringPass>());
    pm.addPass(std::make_unique<RegAllocPass>());
    pm.addPass(std::make_unique<BlockLayoutPass>());

    Diagnostics diags;
    EXPECT_TRUE(pm.run(m, diags));
    ASSERT_FALSE(m.mir.empty());
    ASSERT_FALSE(m.mir[0].blocks.empty());

    // The first block's name must contain "entry" (the IL entry block).
    const std::string &firstName = m.mir[0].blocks[0].name;
    const bool isEntry = firstName.find("entry") != std::string::npos;
    if (!isEntry)
        std::cerr << "First block is '" << firstName << "', expected entry block.\n";
    EXPECT_TRUE(isEntry);
}

// ---------------------------------------------------------------------------
// Test 5: BlockLayoutPass integrates cleanly in the full PassManager.
// ---------------------------------------------------------------------------
TEST(AArch64BlockLayout, PipelineIntegration)
{
    const std::string il =
        "il 0.1\n"
        "func @layout_integration() -> i64 {\n"
        "entry:\n"
        "  %a = add 10, 20\n"
        "  %b = mul %a, 3\n"
        "  ret %b\n"
        "}\n";

    il::core::Module mod = parseIL(il);
    ASSERT_FALSE(mod.functions.empty());

    const TargetInfo &ti = darwinTarget();
    AArch64Module m;
    m.ilMod = &mod;
    m.ti    = &ti;

    Diagnostics diags;
    EXPECT_TRUE(buildLayoutPipeline().run(m, diags));
    EXPECT_TRUE(diags.errors().empty());
    EXPECT_FALSE(m.assembly.empty());
    EXPECT_NE(m.assembly.find("layout_integration"), std::string::npos);
    EXPECT_NE(m.assembly.find("ret"), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
