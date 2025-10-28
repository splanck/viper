// File: tests/vm/BranchingHelperTests.cpp
// Purpose: Validate shared branching helpers for VM control flow.
// Key invariants: Case selection honours range and default behaviour while
//                 signalling invalid fall-through conditions.
// Ownership/Lifetime: Exercises helpers with synthetic tables without executing
//                     full VM programs.
// Links: docs/il-guide.md#reference

#include "vm/ops/common/Branching.hpp"

#include "vm/control_flow.hpp"

#include <cassert>
#include <vector>

using namespace il::vm::detail::ops::common;
using viper::vm::SwitchCacheEntry;

namespace
{
SwitchCacheEntry makeSortedEntry(std::initializer_list<std::pair<int32_t, int32_t>> pairs,
                                 int32_t defaultIdx)
{
    viper::vm::SortedCases sorted;
    for (auto [key, idx] : pairs)
    {
        sorted.keys.push_back(key);
        sorted.targetIdx.push_back(idx);
    }
    SwitchCacheEntry entry{};
    entry.kind = SwitchCacheEntry::Sorted;
    entry.defaultIdx = defaultIdx;
    entry.backend = std::move(sorted);
    return entry;
}
} // namespace

int main()
{
    {
        auto entry = makeSortedEntry({{1, 1}, {5, 2}, {7, 3}}, 0);
        Target defaultTgt{};
        defaultTgt.valid = true;
        defaultTgt.labelIndex = 0;
        defaultTgt.cache = &entry;

        std::vector<Case> cases;
        cases.push_back(Case{1, 1, Target{.labelIndex = 1, .cache = &entry, .valid = true}});
        cases.push_back(Case{5, 5, Target{.labelIndex = 2, .cache = &entry, .valid = true}});
        cases.push_back(Case{7, 7, Target{.labelIndex = 3, .cache = &entry, .valid = true}});

        Target selected = select_case(5, cases, defaultTgt);
        assert(selected.valid);
        assert(selected.labelIndex == 2);
    }

    {
        std::vector<Case> cases;
        cases.push_back(Case{10, 20, Target{.labelIndex = 1, .valid = true}});
        cases.push_back(Case{30, 40, Target{.labelIndex = 2, .valid = true}});
        Target defaultTgt{};
        defaultTgt.valid = true;
        defaultTgt.labelIndex = 0;

        Target selected = select_case(18, cases, defaultTgt);
        assert(selected.valid);
        assert(selected.labelIndex == 1);
    }

    {
        auto entry = makeSortedEntry({{2, 1}, {4, 2}}, 0);
        Target defaultTgt{};
        defaultTgt.valid = true;
        defaultTgt.labelIndex = 0;
        defaultTgt.cache = &entry;

        std::vector<Case> cases;
        cases.push_back(Case{2, 2, Target{.labelIndex = 1, .cache = &entry, .valid = true}});
        cases.push_back(Case{4, 4, Target{.labelIndex = 2, .cache = &entry, .valid = true}});

        Target selected = select_case(99, cases, defaultTgt);
        assert(selected.valid);
        assert(selected.labelIndex == 0);
    }

    {
        auto entry = makeSortedEntry({{3, 1}}, -1);
        Target defaultTgt{};
        defaultTgt.valid = false;
        defaultTgt.labelIndex = 0;
        defaultTgt.cache = &entry;

        std::vector<Case> cases;
        cases.push_back(Case{3, 3, Target{.labelIndex = 1, .cache = &entry, .valid = true}});

        Target selected = select_case(42, cases, defaultTgt);
        assert(!selected.valid);
    }

    return 0;
}

