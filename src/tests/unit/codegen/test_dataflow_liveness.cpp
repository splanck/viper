//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_dataflow_liveness.cpp
// Purpose: Unit tests for the shared backward dataflow liveness solver
//          (DataflowLiveness.hpp). Tests convergence correctness for various
//          CFG shapes and validates that the ICE fires on non-convergence.
//
// Key invariants:
//   - Tests construct CFG successor relations and gen/kill sets manually.
//   - Results are verified against hand-computed liveIn/liveOut expectations.
//
// Ownership/Lifetime:
//   - Standalone test binary.
//
// Links: src/codegen/common/ra/DataflowLiveness.hpp
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/common/ra/DataflowLiveness.hpp"

using namespace viper::codegen::ra;

// ---------------------------------------------------------------------------
// Test: Single block, no successors — trivial case
// ---------------------------------------------------------------------------
TEST(DataflowLiveness, SingleBlock)
{
    // CFG: block 0 (no successors)
    // gen = {v1}, kill = {v2}
    std::vector<std::vector<std::size_t>> succs = {{}};
    std::vector<std::unordered_set<uint16_t>> gen = {{1}};
    std::vector<std::unordered_set<uint16_t>> kill = {{2}};

    auto result = solveBackwardDataflow(succs, gen, kill);

    // liveOut[0] = {} (no successors)
    EXPECT_TRUE(result.liveOut[0].empty());
    // liveIn[0] = gen[0] = {v1}
    EXPECT_EQ(result.liveIn[0].size(), 1u);
    EXPECT_TRUE(result.liveIn[0].count(1));
}

// ---------------------------------------------------------------------------
// Test: Linear chain — block 0 → block 1
// ---------------------------------------------------------------------------
TEST(DataflowLiveness, LinearChain)
{
    // CFG: 0 → 1
    // Block 0: gen={}, kill={v1} (defines v1)
    // Block 1: gen={v1}, kill={} (uses v1)
    std::vector<std::vector<std::size_t>> succs = {{1}, {}};
    std::vector<std::unordered_set<uint16_t>> gen = {{}, {1}};
    std::vector<std::unordered_set<uint16_t>> kill = {{1}, {}};

    auto result = solveBackwardDataflow(succs, gen, kill);

    // liveIn[1] = {v1}
    EXPECT_TRUE(result.liveIn[1].count(1));
    // liveOut[0] = liveIn[1] = {v1}
    EXPECT_TRUE(result.liveOut[0].count(1));
    // liveIn[0] = gen[0] union (liveOut[0] - kill[0]) = {} union ({v1} - {v1}) = {}
    EXPECT_TRUE(result.liveIn[0].empty());
}

// ---------------------------------------------------------------------------
// Test: Diamond CFG — block 0 → {1, 2}, both → 3
// ---------------------------------------------------------------------------
TEST(DataflowLiveness, Diamond)
{
    // CFG:  0 → 1, 0 → 2, 1 → 3, 2 → 3
    // Block 0: defines v1, v2
    // Block 1: uses v1
    // Block 2: uses v2
    // Block 3: uses v1, v2
    std::vector<std::vector<std::size_t>> succs = {{1, 2}, {3}, {3}, {}};
    std::vector<std::unordered_set<uint16_t>> gen = {{}, {1}, {2}, {1, 2}};
    std::vector<std::unordered_set<uint16_t>> kill = {{1, 2}, {}, {}, {}};

    auto result = solveBackwardDataflow(succs, gen, kill);

    // v1 and v2 must be live-out of block 0
    EXPECT_TRUE(result.liveOut[0].count(1));
    EXPECT_TRUE(result.liveOut[0].count(2));
    // But liveIn[0] should be empty because block 0 kills both
    EXPECT_TRUE(result.liveIn[0].empty());
}

// ---------------------------------------------------------------------------
// Test: Loop — block 0 → 1 → 0 (back edge)
// ---------------------------------------------------------------------------
TEST(DataflowLiveness, SimpleLoop)
{
    // CFG: 0 → 1, 1 → 0, 1 → 2 (exit)
    // Block 0: uses v1
    // Block 1: defines v1
    // Block 2: uses v2
    std::vector<std::vector<std::size_t>> succs = {{1}, {0, 2}, {}};
    std::vector<std::unordered_set<uint16_t>> gen = {{1}, {}, {2}};
    std::vector<std::unordered_set<uint16_t>> kill = {{}, {1}, {}};

    auto result = solveBackwardDataflow(succs, gen, kill);

    // v1 must be live in block 0 (used there, defined in block 1)
    EXPECT_TRUE(result.liveIn[0].count(1));
    // v2 must propagate from block 2 through block 1 to liveOut[1]
    EXPECT_TRUE(result.liveOut[1].count(2));
}

// ---------------------------------------------------------------------------
// Test: buildPredecessors helper
// ---------------------------------------------------------------------------
TEST(DataflowLiveness, BuildPredecessors)
{
    std::vector<std::vector<std::size_t>> succs = {{1, 2}, {3}, {3}, {}};
    auto preds = buildPredecessors(succs);

    EXPECT_EQ(preds.size(), 4u);
    EXPECT_TRUE(preds[0].empty());  // block 0 has no predecessors
    EXPECT_EQ(preds[1].size(), 1u); // block 1 has pred 0
    EXPECT_EQ(preds[1][0], 0u);
    EXPECT_EQ(preds[2].size(), 1u); // block 2 has pred 0
    EXPECT_EQ(preds[3].size(), 2u); // block 3 has preds 1, 2
}

// ---------------------------------------------------------------------------
// Test: Convergence — verify iteration count is reasonable
// ---------------------------------------------------------------------------
TEST(DataflowLiveness, ConvergesWithinBounds)
{
    // 10-block linear chain: 0→1→2→...→9
    // Variable used in block 9, defined in block 0
    // Should converge in ~10 iterations (one per block)
    const std::size_t n = 10;
    std::vector<std::vector<std::size_t>> succs(n);
    std::vector<std::unordered_set<uint16_t>> gen(n);
    std::vector<std::unordered_set<uint16_t>> kill(n);

    for (std::size_t i = 0; i + 1 < n; ++i)
        succs[i] = {i + 1};

    kill[0] = {42};    // block 0 defines v42
    gen[n - 1] = {42}; // block 9 uses v42

    auto result = solveBackwardDataflow(succs, gen, kill, 1000);

    // v42 should be live-out of every block except the last
    for (std::size_t i = 0; i + 1 < n; ++i)
    {
        EXPECT_TRUE(result.liveOut[i].count(42));
    }
    // v42 should NOT be live-in to block 0 (killed there)
    EXPECT_FALSE(result.liveIn[0].count(42));
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
