// File: lib/VM/Debug.cpp
// Purpose: Implement breakpoint control and path normalization for the VM.
// Key invariants: Interned labels identify block breakpoints; source line
// breakpoints match when either the normalized file path and line or the
// basename and line coincide. Ownership/Lifetime: DebugCtrl owns its interner,
// breakpoint set, and source line list.
// Links: docs/dev/vm.md
#include "VM/Debug.h"
#include "il/core/Instr.hpp"
#include "support/source_manager.hpp"
#include <filesystem>
#include <iostream>

namespace il::vm
{

std::string DebugCtrl::normalizePath(std::string p)
{
    std::replace(p.begin(), p.end(), '\\', '/');
    namespace fs = std::filesystem;
    fs::path path{p};
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(path, ec);
    if (ec)
        canon = path.lexically_normal();
    fs::path rel = canon.lexically_relative(fs::current_path());
    if (rel.empty())
        rel = canon;
    std::string out = rel.generic_string();
    if (out.empty())
        out = ".";
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
    std::string normFile = normalizePath(std::move(file));
    size_t pos = normFile.find_last_of('/');
    std::string base = (pos == std::string::npos) ? normFile : normFile.substr(pos + 1);
    srcLineBPs_.push_back({normFile, base, line});
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

    // Resolve the instruction's source file, normalize the path, and derive its
    // basename. A breakpoint matches if both the line number and either the
    // normalized path or basename are equal.
    std::string normFile = normalizePath(std::string(sm_->getPath(I.loc.file_id)));
    size_t pos = normFile.find_last_of('/');
    std::string base = (pos == std::string::npos) ? normFile : normFile.substr(pos + 1);
    int line = static_cast<int>(I.loc.line);
    for (const auto &bp : srcLineBPs_)
    {
        if (line != bp.line)
            continue;
        auto check = [&](const std::string &key)
        {
            if (lastHitSrc_ && lastHitSrc_->first == key && lastHitSrc_->second == line)
                return false;
            lastHitSrc_ = std::make_pair(key, line);
            return true;
        };
        if (normFile == bp.normFile)
        {
            if (check(bp.normFile))
                return true;
        }
        else if (base == bp.base)
        {
            if (check(bp.base))
                return true;
        }
    }
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

void DebugCtrl::resetLastHit()
{
    lastHitSrc_.reset();
}

} // namespace il::vm
