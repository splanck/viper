// File: lib/VM/Debug.h
// Purpose: Declare breakpoint and debug control for VM.
// Key invariants: Breakpoints match block labels via shared interner.
// Ownership/Lifetime: DebugCtrl holds non-owning pointer to interner.
// Links: docs/dev/vm.md
#pragma once

#include "support/symbol.hpp"
#include <vector>

namespace il::core
{
struct BasicBlock;
}

namespace il::support
{
class StringInterner;
}

namespace il::vm
{

struct Breakpoint
{
    support::Symbol label;
};

class DebugCtrl
{
  public:
    explicit DebugCtrl(il::support::StringInterner &si);
    void addBreak(il::support::Symbol label);
    bool shouldBreak(const il::core::BasicBlock &blk) const;

  private:
    il::support::StringInterner *interner;
    std::vector<Breakpoint> breaks;
};

} // namespace il::vm
