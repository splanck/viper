// File: lib/VM/Debug.cpp
// Purpose: Implement breakpoint control for the VM.
// Key invariants: Interned labels or file paths uniquely identify breakpoints.
// Ownership/Lifetime: DebugCtrl owns its interner and breakpoint set.
// Links: docs/dev/vm.md
#include "VM/Debug.h"
#include "il/core/Instr.hpp"
#include "support/source_manager.hpp"
#include <iostream>

namespace il::vm
{

il::support::Symbol DebugCtrl::internLabel(std::string_view label)
{
    return interner_.intern(label);
}

void DebugCtrl::addBreak(il::support::Symbol sym)
{
    if (sym)
        breaks_.emplace_back(sym);
}

void DebugCtrl::addBreak(il::support::Symbol file, int line)
{
    if (file && line > 0)
        breaks_.emplace_back(file, line);
}

bool DebugCtrl::shouldBreak(const il::core::BasicBlock &blk) const
{
    il::support::Symbol sym = interner_.intern(blk.label);
    for (const auto &b : breaks_)
        if (b.kind == Breakpoint::Label && b.label == sym)
            return true;
    return false;
}

bool DebugCtrl::shouldBreak(const il::core::Instr &in) const
{
    if (!sm_ || !in.loc.isValid())
    {
        lastSrc_.reset();
        return false;
    }
    il::support::Symbol fileSym = interner_.intern(sm_->getPath(in.loc.file_id));
    for (const auto &b : breaks_)
    {
        if (b.kind == Breakpoint::SrcLine && b.src.file == fileSym &&
            b.src.line == static_cast<int>(in.loc.line))
        {
            if (lastSrc_ && lastSrc_->file == fileSym &&
                lastSrc_->line == static_cast<int>(in.loc.line))
                return false;
            lastSrc_ = SrcPos{fileSym, static_cast<int>(in.loc.line)};
            return true;
        }
    }
    lastSrc_.reset();
    return false;
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
