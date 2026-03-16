//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_ra_victim_selection.cpp
// Purpose: Unit tests for the shared register allocator victim selection
//          algorithms (furthest-first and LRU heuristics).
//
// Key invariants:
//   - selectFurthestVictim picks the vreg with the most distant next use
//   - selectLRUVictim picks the vreg that was used least recently
//   - Both handle empty sets, single elements, and ties correctly
//
// Ownership/Lifetime:
//   - Standalone test binary.
//
// Links: src/codegen/common/ra/ArchTraits.hpp,
//        plans/audit-08-shared-regalloc.md
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/common/ra/ArchTraits.hpp"

#include <climits>
#include <unordered_map>

using namespace viper::codegen::ra;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

/// Simple next-use map for testing.
struct NextUseMap
{
    std::unordered_map<uint16_t, unsigned> distances;

    unsigned operator()(uint16_t vreg) const
    {
        auto it = distances.find(vreg);
        return (it != distances.end()) ? it->second : UINT_MAX;
    }
};

/// Simple last-use map for testing.
struct LastUseMap
{
    std::unordered_map<uint16_t, unsigned> times;

    unsigned operator()(uint16_t vreg) const
    {
        auto it = times.find(vreg);
        return (it != times.end()) ? it->second : 0;
    }
};

} // namespace

// ===========================================================================
// Furthest-First Victim Selection
// ===========================================================================

TEST(VictimSelection, FurthestEmpty)
{
    std::vector<uint16_t> active;
    NextUseMap map{};
    EXPECT_EQ(selectFurthestVictim(active, map), 0u);
}

TEST(VictimSelection, FurthestSingle)
{
    std::vector<uint16_t> active = {42};
    NextUseMap map{{{42, 10}}};
    EXPECT_EQ(selectFurthestVictim(active, map), 42u);
}

TEST(VictimSelection, FurthestPicksMostDistant)
{
    // v1 at distance 5, v2 at distance 20, v3 at distance 10
    std::vector<uint16_t> active = {1, 2, 3};
    NextUseMap map{{{1, 5}, {2, 20}, {3, 10}}};
    EXPECT_EQ(selectFurthestVictim(active, map), 2u);
}

TEST(VictimSelection, FurthestNoFutureUse)
{
    // v1 has a use at 5; v2 has no future use (UINT_MAX)
    std::vector<uint16_t> active = {1, 2};
    NextUseMap map{{{1, 5}}};
    // v2 not in map → UINT_MAX → picked as victim
    EXPECT_EQ(selectFurthestVictim(active, map), 2u);
}

TEST(VictimSelection, FurthestTieBreaksToFirst)
{
    // All at same distance → first in active set wins
    std::vector<uint16_t> active = {10, 20, 30};
    NextUseMap map{{{10, 100}, {20, 100}, {30, 100}}};
    EXPECT_EQ(selectFurthestVictim(active, map), 10u);
}

TEST(VictimSelection, FurthestLargeSet)
{
    // 100 vregs, one at distance 1000
    std::vector<uint16_t> active;
    NextUseMap map{};
    for (uint16_t i = 1; i <= 100; ++i)
    {
        active.push_back(i);
        map.distances[i] = i; // distance = vreg id
    }
    map.distances[50] = 1000; // v50 is the outlier
    EXPECT_EQ(selectFurthestVictim(active, map), 50u);
}

// ===========================================================================
// LRU Victim Selection
// ===========================================================================

TEST(VictimSelection, LRUEmpty)
{
    std::vector<uint16_t> active;
    LastUseMap map{};
    EXPECT_EQ(selectLRUVictim(active, map), 0u);
}

TEST(VictimSelection, LRUSingle)
{
    std::vector<uint16_t> active = {7};
    LastUseMap map{{{7, 42}}};
    EXPECT_EQ(selectLRUVictim(active, map), 7u);
}

TEST(VictimSelection, LRUPicksOldest)
{
    // v1 last used at 100, v2 at 5, v3 at 50
    std::vector<uint16_t> active = {1, 2, 3};
    LastUseMap map{{{1, 100}, {2, 5}, {3, 50}}};
    EXPECT_EQ(selectLRUVictim(active, map), 2u);
}

TEST(VictimSelection, LRUNeverUsed)
{
    // v1 used at 10; v2 never used (defaults to 0)
    std::vector<uint16_t> active = {1, 2};
    LastUseMap map{{{1, 10}}};
    // v2 not in map → 0 → picked as LRU
    EXPECT_EQ(selectLRUVictim(active, map), 2u);
}

TEST(VictimSelection, LRUWithLambda)
{
    // Test with a lambda instead of a struct
    std::vector<uint16_t> active = {1, 2, 3};
    auto getLastUse = [](uint16_t vreg) -> unsigned
    {
        switch (vreg)
        {
            case 1:
                return 50;
            case 2:
                return 10;
            case 3:
                return 30;
            default:
                return 0;
        }
    };
    EXPECT_EQ(selectLRUVictim(active, getLastUse), 2u);
}

TEST(VictimSelection, FurthestWithLambda)
{
    // Test furthest with a lambda
    std::vector<uint16_t> active = {1, 2, 3};
    auto getNextUse = [](uint16_t vreg) -> unsigned
    {
        switch (vreg)
        {
            case 1:
                return 5;
            case 2:
                return 100;
            case 3:
                return 20;
            default:
                return UINT_MAX;
        }
    };
    EXPECT_EQ(selectFurthestVictim(active, getNextUse), 2u);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
