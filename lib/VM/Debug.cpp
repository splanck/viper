// File: lib/VM/Debug.cpp
// Purpose: Implement breakpoint control for the VM.
// Key invariants: Interned labels uniquely identify breakpoints.
// Ownership/Lifetime: DebugCtrl owns its interner and breakpoint set.
// Links: docs/dev/vm.md
#include "VM/Debug.h"
#include <algorithm>
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
    {
        Breakpoint bp;
        bp.kind = Breakpoint::Kind::Label;
        bp.label = sym;
        breaks_.push_back(std::move(bp));
    }
}

void DebugCtrl::addSrcBreak(std::string file, int line)
{
    Breakpoint bp;
    bp.kind = Breakpoint::Kind::SrcLine;
    bp.file = std::move(file);
    bp.line = line;
    breaks_.push_back(std::move(bp));
}

bool DebugCtrl::shouldBreak(const il::core::BasicBlock &blk) const
{
    il::support::Symbol sym = interner_.intern(blk.label);
    return std::any_of(breaks_.begin(),
                       breaks_.end(),
                       [sym](const Breakpoint &b)
                       { return b.kind == Breakpoint::Kind::Label && b.label == sym; });
}

bool DebugCtrl::shouldBreak(const il::core::Instr &in)
{
    if (!sm_ || !in.loc.isValid())
        return false;
    std::string path(sm_->getPath(in.loc.file_id));
    for (auto it = breaks_.begin(); it != breaks_.end(); ++it)
    {
        if (it->kind == Breakpoint::Kind::SrcLine && it->line == static_cast<int>(in.loc.line) &&
            it->file == path)
        {
            breaks_.erase(it);
            return true;
        }
    }
    return false;
}

void DebugCtrl::setSourceManager(const il::support::SourceManager *sm)
{
    sm_ = sm;
}

const il::support::SourceManager *DebugCtrl::getSourceManager() const
{
    return sm_;
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
