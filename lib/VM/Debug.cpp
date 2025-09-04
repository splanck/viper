// File: lib/VM/Debug.cpp
// Purpose: Implement breakpoint management for VM.
// Key invariants: Breakpoints are compared using interned symbols.
// Ownership/Lifetime: DebugCtrl does not own the interner.
// Links: docs/dev/vm.md

#include "VM/Debug.h"

#include "il/core/BasicBlock.hpp"
#include "support/string_interner.hpp"

namespace il::vm
{

DebugCtrl::DebugCtrl(il::support::StringInterner &si) : interner(&si) {}

void DebugCtrl::addBreak(il::support::Symbol label)
{
    breaks.push_back({label});
}

bool DebugCtrl::shouldBreak(const il::core::BasicBlock &blk) const
{
    if (!interner)
        return false;
    il::support::Symbol sym = interner->intern(blk.label);
    for (const auto &bp : breaks)
        if (bp.label == sym)
            return true;
    return false;
}

} // namespace il::vm
