// File: lib/VM/Debug.cpp
// Purpose: Implement breakpoint control for the VM.
// Key invariants: Interned labels and normalized file paths (with basename) uniquely identify
// breakpoints. Ownership/Lifetime: DebugCtrl owns its interner, breakpoint set, and source line
// list. Links: docs/dev/vm.md
#include "VM/Debug.h"
#include "il/core/Instr.hpp"
#include "support/source_manager.hpp"
#include <iostream>
#include <vector>

namespace il::vm
{

std::string DebugCtrl::normalizePath(std::string p)
{
    for (char &c : p)
        if (c == '\\')
            c = '/';
    std::vector<std::string> parts;
    std::string segment;
    bool absolute = !p.empty() && p[0] == '/';
    for (size_t i = 0; i <= p.size(); ++i)
    {
        char ch = (i < p.size()) ? p[i] : '/';
        if (ch == '/')
        {
            if (segment == "" || segment == ".")
            {
                // skip
            }
            else if (segment == "..")
            {
                if (!parts.empty() && parts.back() != "..")
                    parts.pop_back();
                else if (!absolute)
                    parts.push_back("..");
            }
            else
            {
                parts.push_back(segment);
            }
            segment.clear();
        }
        else
        {
            segment.push_back(ch);
        }
    }
    std::string out;
    if (absolute)
        out.push_back('/');
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i)
            out.push_back('/');
        out += parts[i];
    }
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
    std::string norm = normalizePath(std::move(file));
    std::string base;
    if (auto pos = norm.find_last_of('/'); pos != std::string::npos)
        base = norm.substr(pos + 1);
    else
        base = norm;
    srcLineBPs_.push_back({std::move(norm), std::move(base), line});
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
    std::string norm = normalizePath(std::string(sm_->getPath(I.loc.file_id)));
    std::string base;
    if (auto pos = norm.find_last_of('/'); pos != std::string::npos)
        base = norm.substr(pos + 1);
    else
        base = norm;
    int line = static_cast<int>(I.loc.line);
    for (const auto &bp : srcLineBPs_)
        if ((norm == bp.normFile && line == bp.line) || (base == bp.base && line == bp.line))
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
