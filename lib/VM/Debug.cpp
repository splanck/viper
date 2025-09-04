// File: lib/VM/Debug.cpp
// Purpose: Implement breakpoint controller for VM execution.
// Key invariants: Interner must outlive controller; label matches are exact.
// Ownership/Lifetime: Controller borrows interner; no dynamic allocation beyond internal set.
// Links: docs/dev/vm.md
#include "VM/Debug.h"

#include "il/core/BasicBlock.hpp"
#include "support/string_interner.hpp"

namespace il::vm
{

DebugCtrl::DebugCtrl(il::support::StringInterner *si) : interner(si) {}

void DebugCtrl::addBreak(il::support::Symbol label)
{
    brks.insert(label);
}

bool DebugCtrl::shouldBreak(const il::core::BasicBlock &blk) const
{
    if (!interner)
        return false;
    il::support::Symbol sym = interner->intern(blk.label);
    return brks.count(sym) > 0;
}

} // namespace il::vm
