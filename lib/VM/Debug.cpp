// File: lib/VM/Debug.cpp
// Purpose: Implement breakpoint control and path normalization for the VM.
// Key invariants: Interned labels and normalized file paths uniquely identify breakpoints.
// Ownership/Lifetime: DebugCtrl owns its interner, breakpoint set, and source line list.
// Links: docs/dev/vm.md
#include "VM/Debug.h"
#include "il/core/Instr.hpp"
#include "support/source_manager.hpp"
#include <iostream>

namespace il::vm
{

std::string DebugCtrl::normalizePath(std::string p)
{
    for (char &c : p)
        if (c == '\\')
            c = '/';
    bool absolute = !p.empty() && p.front() == '/';
    std::vector<std::string> stack;
    std::string segment;
    auto flush = [&]()
    {
        if (segment.empty() || segment == ".")
        {
            segment.clear();
            return;
        }
        if (segment == "..")
        {
            if (!stack.empty() && stack.back() != "..")
                stack.pop_back();
            else if (!absolute)
                stack.push_back("..");
        }
        else
            stack.push_back(std::move(segment));
        segment.clear();
    };
    for (size_t i = 0; i <= p.size(); ++i)
    {
        char c = (i < p.size()) ? p[i] : '/';
        if (c == '/')
            flush();
        else
            segment += c;
    }
    std::string out = absolute ? "/" : "";
    for (size_t i = 0; i < stack.size(); ++i)
    {
        if (i)
            out += '/';
        out += stack[i];
    }
    if (out.empty())
        return absolute ? std::string{"/"} : std::string{"."};
    return out;
}

il::support::Symbol DebugCtrl::internLabel(std::string_view label)
{
    return interner_.intern(label);
}

void DebugCtrl::addBreak(il::support::Symbol sym)
{
    if (sym)
        breaks_.insert(sym);
}

bool DebugCtrl::shouldBreak(const il::core::BasicBlock &blk) const
{
    il::support::Symbol sym = interner_.intern(blk.label);
    return breaks_.count(sym) != 0;
}

void DebugCtrl::addBreakSrcLine(std::string file, int line)
{
    srcLineBPs_.push_back({normalizePath(std::move(file)), line});
}

bool DebugCtrl::hasSrcLineBPs() const
{
    return !srcLineBPs_.empty();
}

void DebugCtrl::setSourceManager(const il::support::SourceManager *sm)
{
    sm_ = sm;
}

const il::support::SourceManager *DebugCtrl::getSourceManager() const
{
    return sm_;
}

bool DebugCtrl::shouldBreakOn(const il::core::Instr &I) const
{
    if (!sm_ || srcLineBPs_.empty() || !I.loc.isValid())
        return false;
    std::string path = normalizePath(std::string(sm_->getPath(I.loc.file_id)));
    for (const auto &bp : srcLineBPs_)
        if (path == bp.file && static_cast<int>(I.loc.line) == bp.line)
            return true;
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
