// File: lib/VM/Debug.cpp
// Purpose: Implement breakpoint management for the VM.
// Key invariants: Breakpoints are matched by exact label string.
// Ownership/Lifetime: DebugCtrl owns all interned labels and breakpoints.
// Links: docs/dev/vm.md

#include "VM/Debug.h"

namespace il::vm
{

il::support::Symbol DebugCtrl::internLabel(const std::string &lbl)
{
    return interner.intern(lbl);
}

void DebugCtrl::addBreak(il::support::Symbol lbl)
{
    bps.push_back({lbl});
}

bool DebugCtrl::shouldBreak(const il::core::BasicBlock &blk) const
{
    for (const auto &bp : bps)
    {
        if (interner.lookup(bp.label) == blk.label)
            return true;
    }
    return false;
}

} // namespace il::vm
