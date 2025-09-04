// File: lib/VM/Debug.cpp
// Purpose: Implement breakpoint control for the VM.
// Key invariants: Interned labels and normalized file paths (with basename fallback)
// uniquely identify breakpoints.
// Ownership/Lifetime: DebugCtrl owns its interner, breakpoint set, and source line list.
// Links: docs/dev/vm.md
#include "VM/Debug.h"
#include "il/core/Instr.hpp"
#include "support/source_manager.hpp"
#include <iostream>
#include <vector>

namespace il::vm
{

std::string DebugCtrl::normalizePath(std::string p)
{
    for (char &ch : p)
        if (ch == '\\')
            ch = '/';

    std::vector<std::string> parts;
    size_t i = 0;
    bool absolute = !p.empty() && p[0] == '/';
    while (i < p.size())
    {
        size_t j = i;
        while (j < p.size() && p[j] != '/')
            ++j;
        std::string_view part(p.data() + i, j - i);
        if (part == "..")
        {
            if (!parts.empty())
                parts.pop_back();
        }
        else if (!part.empty() && part != ".")
        {
            parts.emplace_back(part);
        }
        i = j + 1;
    }

    std::string out;
    if (absolute)
        out.push_back('/');
    for (size_t k = 0; k < parts.size(); ++k)
    {
        if (k)
            out.push_back('/');
        out += parts[k];
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
    size_t pos = norm.rfind('/');
    std::string base = (pos == std::string::npos) ? norm : norm.substr(pos + 1);
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
    std::string path(sm_->getPath(I.loc.file_id));
    std::string norm = normalizePath(std::move(path));
    size_t pos = norm.rfind('/');
    std::string base = (pos == std::string::npos) ? norm : norm.substr(pos + 1);
    for (const auto &bp : srcLineBPs_)
        if (static_cast<int>(I.loc.line) == bp.line && (norm == bp.normFile || base == bp.base))
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
