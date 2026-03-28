//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/codegen/common/test_dataflow_liveness.cpp
// Purpose: Unit tests for the shared backward dataflow liveness solver.
//          Verifies correct liveIn/liveOut computation on synthetic CFGs
//          without depending on any specific MIR representation.
// Key invariants:
//   - liveOut[B] = union of liveIn[S] for all successors S of B
//   - liveIn[B]  = gen[B] union (liveOut[B] - kill[B])
// Links: src/codegen/common/ra/DataflowLiveness.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/ra/DataflowLiveness.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

// ---------------------------------------------------------------------------
// Minimal test harness (matches linker test style)
// ---------------------------------------------------------------------------

static int gFail = 0;
static int gPass = 0;

#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, (msg));                      \
            ++gFail;                                                                               \
        } else {                                                                                   \
            ++gPass;                                                                               \
        }                                                                                          \
    } while (0)

// ---------------------------------------------------------------------------

using viper::codegen::ra::buildPredecessors;
using viper::codegen::ra::DataflowResult;
using viper::codegen::ra::solveBackwardDataflow;

// ---------------------------------------------------------------------------
// Test 1: Single straight-line block — no cross-block liveness.
// ---------------------------------------------------------------------------
static void testSingleBlock() {
    // Block 0: gen={1}, kill={2} — no successors.
    std::vector<std::vector<std::size_t>> succs = {{}};
    std::vector<std::unordered_set<uint16_t>> gen = {{1}};
    std::vector<std::unordered_set<uint16_t>> kill = {{2}};

    auto r = solveBackwardDataflow(succs, gen, kill);

    CHECK(r.liveOut[0].empty(), "single block: liveOut should be empty");
    CHECK(r.liveIn[0].count(1) == 1, "single block: vreg 1 in liveIn (gen)");
    CHECK(r.liveIn[0].count(2) == 0, "single block: vreg 2 not in liveIn (killed)");
}

// ---------------------------------------------------------------------------
// Test 2: Linear chain B0 -> B1 -> B2.
// v1 used in B2, defined nowhere — should propagate back to B0's liveIn.
// ---------------------------------------------------------------------------
static void testLinearChain() {
    // B0 -> B1 -> B2
    std::vector<std::vector<std::size_t>> succs = {{1}, {2}, {}};
    std::vector<std::unordered_set<uint16_t>> gen = {{}, {}, {1}};
    std::vector<std::unordered_set<uint16_t>> kill = {{}, {}, {}};

    auto r = solveBackwardDataflow(succs, gen, kill);

    CHECK(r.liveIn[2].count(1) == 1, "linear chain: v1 in B2 liveIn");
    CHECK(r.liveOut[1].count(1) == 1, "linear chain: v1 in B1 liveOut");
    CHECK(r.liveIn[1].count(1) == 1, "linear chain: v1 in B1 liveIn (pass-through)");
    CHECK(r.liveOut[0].count(1) == 1, "linear chain: v1 in B0 liveOut");
    CHECK(r.liveIn[0].count(1) == 1, "linear chain: v1 in B0 liveIn (pass-through)");
}

// ---------------------------------------------------------------------------
// Test 3: Kill stops propagation.
// B0 -> B1 -> B2.  v1 used in B2, defined in B1.
// ---------------------------------------------------------------------------
static void testKillStopsPropagation() {
    std::vector<std::vector<std::size_t>> succs = {{1}, {2}, {}};
    std::vector<std::unordered_set<uint16_t>> gen = {{}, {}, {1}};
    std::vector<std::unordered_set<uint16_t>> kill = {{}, {1}, {}};

    auto r = solveBackwardDataflow(succs, gen, kill);

    CHECK(r.liveOut[1].count(1) == 1, "kill stops: v1 in B1 liveOut");
    CHECK(r.liveIn[1].count(1) == 0, "kill stops: v1 NOT in B1 liveIn (killed)");
    CHECK(r.liveOut[0].count(1) == 0, "kill stops: v1 NOT in B0 liveOut");
    CHECK(r.liveIn[0].count(1) == 0, "kill stops: v1 NOT in B0 liveIn");
}

// ---------------------------------------------------------------------------
// Test 4: Diamond CFG with merge.
//         B0 -> B1, B0 -> B2, B1 -> B3, B2 -> B3.
//         v1 used in B3 only.
// ---------------------------------------------------------------------------
static void testDiamond() {
    std::vector<std::vector<std::size_t>> succs = {{1, 2}, {3}, {3}, {}};
    std::vector<std::unordered_set<uint16_t>> gen = {{}, {}, {}, {1}};
    std::vector<std::unordered_set<uint16_t>> kill = {{}, {}, {}, {}};

    auto r = solveBackwardDataflow(succs, gen, kill);

    // v1 should propagate up through both paths.
    CHECK(r.liveOut[1].count(1) == 1, "diamond: v1 in B1 liveOut");
    CHECK(r.liveOut[2].count(1) == 1, "diamond: v1 in B2 liveOut");
    CHECK(r.liveOut[0].count(1) == 1, "diamond: v1 in B0 liveOut");
}

// ---------------------------------------------------------------------------
// Test 5: Loop — B0 -> B1, B1 -> B1 (self-loop), B1 -> B2.
//         v1 used in B1, defined in B1 (use-before-def within loop body).
// ---------------------------------------------------------------------------
static void testLoop() {
    std::vector<std::vector<std::size_t>> succs = {{1}, {1, 2}, {}};
    std::vector<std::unordered_set<uint16_t>> gen = {{}, {1}, {}};
    std::vector<std::unordered_set<uint16_t>> kill = {{}, {1}, {}};

    auto r = solveBackwardDataflow(succs, gen, kill);

    // v1 is in gen[B1] so it's in liveIn[B1], which means liveOut[B0].
    CHECK(r.liveIn[1].count(1) == 1, "loop: v1 in B1 liveIn (gen before kill)");
    CHECK(r.liveOut[0].count(1) == 1, "loop: v1 in B0 liveOut");
}

// ---------------------------------------------------------------------------
// Test 6: Multiple variables with different lifetimes.
//         B0 -> B1.  v1 used in B1, v2 defined in B0 and used in B1.
// ---------------------------------------------------------------------------
static void testMultipleVars() {
    std::vector<std::vector<std::size_t>> succs = {{1}, {}};
    std::vector<std::unordered_set<uint16_t>> gen = {{}, {1, 2}};
    std::vector<std::unordered_set<uint16_t>> kill = {{2}, {}};

    auto r = solveBackwardDataflow(succs, gen, kill);

    CHECK(r.liveOut[0].count(1) == 1, "multi-var: v1 in B0 liveOut");
    CHECK(r.liveOut[0].count(2) == 1, "multi-var: v2 in B0 liveOut");
    CHECK(r.liveIn[0].count(1) == 1, "multi-var: v1 in B0 liveIn (pass-through)");
    CHECK(r.liveIn[0].count(2) == 0, "multi-var: v2 NOT in B0 liveIn (killed in B0)");
}

// ---------------------------------------------------------------------------
// Test 7: Empty CFG — no blocks, no crash.
// ---------------------------------------------------------------------------
static void testEmptyCFG() {
    std::vector<std::vector<std::size_t>> succs;
    std::vector<std::unordered_set<uint16_t>> gen;
    std::vector<std::unordered_set<uint16_t>> kill;

    auto r = solveBackwardDataflow(succs, gen, kill);
    CHECK(r.liveIn.empty(), "empty CFG: liveIn empty");
    CHECK(r.liveOut.empty(), "empty CFG: liveOut empty");
}

// ---------------------------------------------------------------------------
// Test 8: buildPredecessors utility.
// ---------------------------------------------------------------------------
static void testBuildPredecessors() {
    //  B0 -> B1, B0 -> B2, B1 -> B3, B2 -> B3
    std::vector<std::vector<std::size_t>> succs = {{1, 2}, {3}, {3}, {}};

    auto preds = buildPredecessors(succs);

    CHECK(preds[0].empty(), "preds: B0 has no predecessors");
    CHECK(preds[1].size() == 1 && preds[1][0] == 0, "preds: B1 pred is B0");
    CHECK(preds[2].size() == 1 && preds[2][0] == 0, "preds: B2 pred is B0");
    CHECK(preds[3].size() == 2, "preds: B3 has two predecessors");
}

// ---------------------------------------------------------------------------
// Test 9: Nested loop — B0 -> B1, B1 -> B2, B2 -> B1, B2 -> B3.
//         v1 defined in B0, used in B2. Should be live across the loop.
// ---------------------------------------------------------------------------
static void testNestedLoop() {
    std::vector<std::vector<std::size_t>> succs = {{1}, {2}, {1, 3}, {}};
    std::vector<std::unordered_set<uint16_t>> gen = {{}, {}, {1}, {}};
    std::vector<std::unordered_set<uint16_t>> kill = {{1}, {}, {}, {}};

    auto r = solveBackwardDataflow(succs, gen, kill);

    CHECK(r.liveOut[0].count(1) == 1, "nested loop: v1 in B0 liveOut");
    CHECK(r.liveIn[0].count(1) == 0, "nested loop: v1 NOT in B0 liveIn (killed)");
    CHECK(r.liveOut[1].count(1) == 1, "nested loop: v1 in B1 liveOut");
    CHECK(r.liveIn[1].count(1) == 1, "nested loop: v1 in B1 liveIn (pass-through)");
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main() {
    testSingleBlock();
    testLinearChain();
    testKillStopsPropagation();
    testDiamond();
    testLoop();
    testMultipleVars();
    testEmptyCFG();
    testBuildPredecessors();
    testNestedLoop();

    printf("dataflow_liveness: %d passed, %d failed\n", gPass, gFail);
    return gFail > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
