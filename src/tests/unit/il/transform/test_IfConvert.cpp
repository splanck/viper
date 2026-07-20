//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/transform/test_IfConvert.cpp
// Purpose: Validate that IfConvert folds branch diamonds and triangles into
//          `select` instructions and refuses unsafe speculation.
// Key invariants: Converted functions still verify; arms containing trapping
//                 or side-effecting instructions are left untouched.
// Ownership/Lifetime: Builds a transient module per test invocation.
// Links: docs/adr/0063-il-select-and-if-conversion.md
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/transform/AnalysisManager.hpp"
#include "il/transform/IfConvert.hpp"
#include "il/verify/Verifier.hpp"
#include "tests/TestHarness.hpp"

#include <sstream>
#include <string>

using namespace il::core;

namespace {

Module parseModule(const std::string &text) {
    Module module;
    std::istringstream input(text);
    auto parsed = il::api::v2::parse_text_expected(input, module);
    ASSERT_TRUE(static_cast<bool>(parsed));
    return module;
}

size_t countOpcode(const Function &fn, Opcode op) {
    size_t count = 0;
    for (const auto &block : fn.blocks)
        for (const auto &instr : block.instructions)
            if (instr.op == op)
                ++count;
    return count;
}

void runIfConvert(Module &module) {
    il::transform::AnalysisRegistry registry;
    il::transform::AnalysisManager manager(module, registry);
    il::transform::IfConvert pass;
    auto preserved = pass.run(module.functions.front(), manager);
    manager.invalidateAfterFunctionPass(preserved, module.functions.front());
}

TEST(IfConvert, CollapsedDiamondFoldsDifferingArgsToSelect) {
    // The canonical shape SimplifyCFG leaves behind: one cbr, both edges to
    // the same block, arguments differing in a single position.
    Module module = parseModule(R"(il 0.3.0
func @main(%c: i1, %x: i64) -> i64 {
entry(%c: i1, %x: i64):
  %m = and %x, 255
  cbr %c, join(4096, %x), join(%m, %x)
join(%v: i64, %p: i64):
  %r = or %v, %p
  ret %r
}
)");
    Function &fn = module.functions.front();
    runIfConvert(module);
    EXPECT_EQ(countOpcode(fn, Opcode::CBr), 0u);
    EXPECT_EQ(countOpcode(fn, Opcode::Select), 1u);
    EXPECT_EQ(countOpcode(fn, Opcode::Br), 1u);
    auto verified = il::verify::Verifier::verify(module);
    EXPECT_TRUE(static_cast<bool>(verified));
}

TEST(IfConvert, CompareCollapsedDiamondPreservesRangeProof) {
    // CheckOpt accepts the plain sub because the compare-refined branch args
    // prove the join parameter is non-negative. Replacing that cbr with a
    // select would erase the currently verifier-visible proof.
    Module module = parseModule(R"(il 0.3.0
func @main(%x: i64) -> i64 {
entry(%x: i64):
  %lt = scmp_lt %x, 0
  cbr %lt, join(0), join(%x)
join(%v: i64):
  %r = sub %v, 1
  ret %r
}
)");
    Function &fn = module.functions.front();
    auto before = il::verify::Verifier::verify(module);
    EXPECT_TRUE(static_cast<bool>(before));

    runIfConvert(module);

    EXPECT_EQ(countOpcode(fn, Opcode::Select), 0u);
    EXPECT_EQ(countOpcode(fn, Opcode::CBr), 1u);
    auto after = il::verify::Verifier::verify(module);
    EXPECT_TRUE(static_cast<bool>(after));
}

TEST(IfConvert, ClassicDiamondHoistsArmsAndErasesBlocks) {
    Module module = parseModule(R"(il 0.3.0
func @main(%c: i1, %x: i64) -> i64 {
entry(%c: i1, %x: i64):
  cbr %c, tarm, farm
tarm:
  %a = xor %x, 1
  br join(%a)
farm:
  %b = and %x, 7
  br join(%b)
join(%v: i64):
  ret %v
}
)");
    Function &fn = module.functions.front();
    runIfConvert(module);
    EXPECT_EQ(countOpcode(fn, Opcode::Select), 1u);
    EXPECT_EQ(countOpcode(fn, Opcode::CBr), 0u);
    // Both arm blocks are erased: entry + join remain.
    EXPECT_EQ(fn.blocks.size(), 2u);
    // The hoisted arm computations now live in the head block.
    EXPECT_EQ(countOpcode(fn, Opcode::Xor), 1u);
    EXPECT_EQ(countOpcode(fn, Opcode::And), 1u);
    auto verified = il::verify::Verifier::verify(module);
    EXPECT_TRUE(static_cast<bool>(verified));
}

TEST(IfConvert, TriangleConverts) {
    Module module = parseModule(R"(il 0.3.0
func @main(%c: i1, %x: i64) -> i64 {
entry(%c: i1, %x: i64):
  cbr %c, arm, join(%x)
arm:
  %t = xor %x, 1
  br join(%t)
join(%v: i64):
  ret %v
}
)");
    Function &fn = module.functions.front();
    runIfConvert(module);
    EXPECT_EQ(countOpcode(fn, Opcode::Select), 1u);
    EXPECT_EQ(countOpcode(fn, Opcode::CBr), 0u);
    EXPECT_EQ(fn.blocks.size(), 2u);
    auto verified = il::verify::Verifier::verify(module);
    EXPECT_TRUE(static_cast<bool>(verified));
}

TEST(IfConvert, TriangleReusesIdenticalJoinArgs) {
    Module module = parseModule(R"(il 0.3.0
func @main(%c: i1, %x: i64) -> i64 {
entry(%c: i1, %x: i64):
  cbr %c, arm, join(%x, %x)
arm:
  %t = xor %x, 1
  br join(%t, %x)
join(%v: i64, %same: i64):
  %r = or %v, %same
  ret %r
}
)");
    Function &fn = module.functions.front();
    runIfConvert(module);
    EXPECT_EQ(countOpcode(fn, Opcode::Select), 1u);
    EXPECT_EQ(countOpcode(fn, Opcode::CBr), 0u);
    auto verified = il::verify::Verifier::verify(module);
    EXPECT_TRUE(static_cast<bool>(verified));
}

TEST(IfConvert, TrappingArmIsNotSpeculated) {
    // sdiv traps on divide-by-zero, so the arm must not be hoisted.
    Module module = parseModule(R"(il 0.3.0
func @main(%c: i1, %x: i64, %d: i64) -> i64 {
entry(%c: i1, %x: i64, %d: i64):
  cbr %c, arm, join(%x)
arm:
  %q = sdiv %x, %d
  br join(%q)
join(%v: i64):
  ret %v
}
)");
    Function &fn = module.functions.front();
    runIfConvert(module);
    EXPECT_EQ(countOpcode(fn, Opcode::Select), 0u);
    EXPECT_EQ(countOpcode(fn, Opcode::CBr), 1u);
    EXPECT_EQ(fn.blocks.size(), 3u);
}

TEST(IfConvert, SideEffectingArmIsNotSpeculated) {
    // iadd.ovf carries the side-effect flag (it can trap), so the opcode-table
    // gate must reject it without needing the explicit exclusion list.
    Module module = parseModule(R"(il 0.3.0
func @main(%c: i1, %x: i64) -> i64 {
entry(%c: i1, %x: i64):
  cbr %c, arm, join(%x)
arm:
  %t = iadd.ovf %x, 1
  br join(%t)
join(%v: i64):
  ret %v
}
)");
    Function &fn = module.functions.front();
    runIfConvert(module);
    EXPECT_EQ(countOpcode(fn, Opcode::Select), 0u);
    EXPECT_EQ(countOpcode(fn, Opcode::CBr), 1u);
}

TEST(IfConvert, DemotedArithmeticArmIsNotSpeculated) {
    // Plain add/sub/mul are spec-rejected opcodes accepted only under a range
    // proof — often derived from exactly the branch this pass would erase
    // (crackman pulseColor/chompAngle regression). They must not be hoisted.
    Module module = parseModule(R"(il 0.3.0
func @main(%c: i1, %x: i64) -> i64 {
entry(%c: i1, %x: i64):
  %m = and %x, 127
  cbr %c, arm, join(%m)
arm:
  %t = sub 255, %m
  br join(%t)
join(%v: i64):
  ret %v
}
)");
    Function &fn = module.functions.front();
    runIfConvert(module);
    EXPECT_EQ(countOpcode(fn, Opcode::Select), 0u);
    EXPECT_EQ(countOpcode(fn, Opcode::CBr), 1u);
}

TEST(IfConvert, OversizedArmIsNotSpeculated) {
    // Four instructions exceed kMaxSpeculatedPerArm (3).
    Module module = parseModule(R"(il 0.3.0
func @main(%c: i1, %x: i64) -> i64 {
entry(%c: i1, %x: i64):
  cbr %c, arm, join(%x)
arm:
  %t0 = xor %x, 1
  %t1 = xor %t0, 2
  %t2 = xor %t1, 3
  %t3 = xor %t2, 4
  br join(%t3)
join(%v: i64):
  ret %v
}
)");
    Function &fn = module.functions.front();
    runIfConvert(module);
    EXPECT_EQ(countOpcode(fn, Opcode::Select), 0u);
    EXPECT_EQ(countOpcode(fn, Opcode::CBr), 1u);
}

TEST(IfConvert, MultiPredecessorArmIsNotConverted) {
    // The arm has a second predecessor, so hoisting would execute it on an
    // unrelated path.
    Module module = parseModule(R"(il 0.3.0
func @main(%c: i1, %c2: i1, %x: i64) -> i64 {
entry(%c: i1, %c2: i1, %x: i64):
  cbr %c, arm, other
other:
  cbr %c2, arm, join(%x)
arm:
  %t = xor %x, 1
  br join(%t)
join(%v: i64):
  ret %v
}
)");
    Function &fn = module.functions.front();
    runIfConvert(module);
    EXPECT_EQ(countOpcode(fn, Opcode::Select), 0u);
}

} // namespace

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
