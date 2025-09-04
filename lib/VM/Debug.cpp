// File: lib/VM/Debug.cpp
// Purpose: Implement breakpoint control for the VM.
// Key invariants: Interned labels uniquely identify breakpoints.
// Ownership/Lifetime: DebugCtrl owns its interner and breakpoint set.
// Links: docs/dev/vm.md
#include "VM/Debug.h"
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

void DebugCtrl::setSourceManager(const il::support::SourceManager *sm)
{
    sm_ = sm;
}

std::optional<Breakpoint::Kind> DebugCtrl::shouldBreak(const il::core::BasicBlock &blk,
                                                       const il::core::Instr &in,
                                                       size_t ip)
{
    for (size_t i = 0; i < breaks_.size(); ++i)
    {
        const Breakpoint &bp = breaks_[i];
        if (bp.kind == Breakpoint::Kind::Label)
        {
            if (ip == 0)
            {
                il::support::Symbol sym = interner_.intern(blk.label);
                if (bp.label == sym)
                    return bp.kind;
            }
        }
        else if (bp.kind == Breakpoint::Kind::SrcLine)
        {
            if (sm_ && in.loc.isValid())
            {
                std::string_view path = sm_->getPath(in.loc.file_id);
                if (bp.file == path && bp.line == static_cast<int>(in.loc.line))
                {
                    breaks_.erase(breaks_.begin() + i);
                    return bp.kind;
                }
            }
        }
    }
    return std::nullopt;
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
