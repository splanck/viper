// File: lib/VM/Debug.cpp
// Purpose: Implement breakpoint control for the VM.
// Key invariants: Interned labels uniquely identify breakpoints.
// Ownership/Lifetime: DebugCtrl owns its interner and breakpoint set.
// Links: docs/dev/vm.md
#include "VM/Debug.h"

namespace il::vm
{

il::support::Symbol DebugCtrl::internLabel(std::string_view label)
{
    return interner_.intern(label);
}

void DebugCtrl::addBreak(il::support::Symbol sym)
{
    if (sym)
        breaks_.insert(sym);
}

void DebugCtrl::setIgnoreBreaks(bool ignore)
{
    ignoreBreaks_ = ignore;
}

bool DebugCtrl::shouldBreak(const il::core::BasicBlock &blk) const
{
    if (ignoreBreaks_)
        return false;
    il::support::Symbol sym = interner_.intern(blk.label);
    return breaks_.count(sym) != 0;
}

} // namespace il::vm
