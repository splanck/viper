// File: lib/VM/Debug.cpp
// Purpose: Implement breakpoint control for the VM.
// Key invariants: Interned labels uniquely identify breakpoints.
// Ownership/Lifetime: DebugCtrl owns its interner and breakpoint set.
// Links: docs/dev/vm.md
#include "VM/Debug.h"
#include "support/source_manager.hpp"
#include <iostream>

namespace il::vm
{

il::support::Symbol DebugCtrl::internLabel(std::string_view label)
{
    return interner_.intern(label);
}

void DebugCtrl::addBreakLabel(il::support::Symbol sym)
{
    if (sym)
        breaks_.emplace_back(sym);
}

void DebugCtrl::addBreakSrc(std::string file, int line)
{
    breaks_.emplace_back(std::move(file), line);
}

bool DebugCtrl::shouldBreakLabel(const il::core::BasicBlock &blk) const
{
    il::support::Symbol sym = interner_.intern(blk.label);
    for (const auto &bp : breaks_)
        if (bp.kind == Breakpoint::Kind::Label && bp.label == sym)
            return true;
    return false;
}

const Breakpoint *DebugCtrl::shouldBreakSrc(const il::support::SourceLoc &loc,
                                            const il::support::SourceManager *sm) const
{
    if (!sm || !loc.isValid())
        return nullptr;
    std::string_view path = sm->getPath(loc.file_id);
    for (const auto &bp : breaks_)
        if (bp.kind == Breakpoint::Kind::SrcLine && bp.src.file == path &&
            bp.src.line == static_cast<int>(loc.line))
            return &bp;
    return nullptr;
}

void DebugCtrl::addWatch(std::string_view name)
{
    il::support::Symbol sym = interner_.intern(name);
    if (sym)
        watches_[sym];
}

void DebugCtrl::onStore(std::string_view name,
                        il::core::Type::Kind ty,
                        int64_t i64,
                        double f64,
                        std::string_view fn,
                        std::string_view blk,
                        size_t ip)
{
    il::support::Symbol sym = interner_.intern(name);
    auto it = watches_.find(sym);
    if (it == watches_.end())
        return;
    WatchEntry &w = it->second;
    if (ty != il::core::Type::Kind::I1 && ty != il::core::Type::Kind::I64 &&
        ty != il::core::Type::Kind::F64)
    {
        std::cerr << "[WATCH] " << name << "=[unsupported]  (fn=@" << fn << " blk=" << blk
                  << " ip=#" << ip << ")\n";
        return;
    }
    bool changed = !w.hasValue;
    if (ty == il::core::Type::Kind::F64)
    {
        if (w.hasValue && w.f64 != f64)
            changed = true;
        if (changed)
            std::cerr << "[WATCH] " << name << "=" << il::core::kindToString(ty) << ":" << f64
                      << "  (fn=@" << fn << " blk=" << blk << " ip=#" << ip << ")\n";
        w.f64 = f64;
    }
    else
    {
        if (w.hasValue && w.i64 != i64)
            changed = true;
        if (changed)
            std::cerr << "[WATCH] " << name << "=" << il::core::kindToString(ty) << ":" << i64
                      << "  (fn=@" << fn << " blk=" << blk << " ip=#" << ip << ")\n";
        w.i64 = i64;
    }
    w.type = ty;
    w.hasValue = true;
}

} // namespace il::vm
