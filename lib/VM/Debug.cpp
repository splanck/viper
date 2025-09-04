// File: lib/VM/Debug.cpp
// Purpose: Implement breakpoint control for the VM.
// Key invariants: Interned labels uniquely identify breakpoints.
// Ownership/Lifetime: DebugCtrl owns its interner and breakpoint set.
// Links: docs/dev/vm.md
#include "VM/Debug.h"
#include <iostream>
#include <utility>

namespace il::vm
{

Breakpoint::Breakpoint(il::support::Symbol sym) : kind(Kind::Label)
{
    label = sym;
}

Breakpoint::Breakpoint(std::string file, int line) : kind(Kind::SrcLine)
{
    new (&src.file) std::string(std::move(file));
    src.line = line;
}

Breakpoint::Breakpoint(const Breakpoint &other) : kind(other.kind)
{
    if (kind == Kind::Label)
        label = other.label;
    else
    {
        new (&src.file) std::string(other.src.file);
        src.line = other.src.line;
    }
}

Breakpoint::Breakpoint(Breakpoint &&other) noexcept : kind(other.kind)
{
    if (kind == Kind::Label)
        label = other.label;
    else
    {
        new (&src.file) std::string(std::move(other.src.file));
        src.line = other.src.line;
    }
}

Breakpoint &Breakpoint::operator=(const Breakpoint &other)
{
    if (this != &other)
    {
        this->~Breakpoint();
        kind = other.kind;
        if (kind == Kind::Label)
            label = other.label;
        else
        {
            new (&src.file) std::string(other.src.file);
            src.line = other.src.line;
        }
    }
    return *this;
}

Breakpoint &Breakpoint::operator=(Breakpoint &&other) noexcept
{
    if (this != &other)
    {
        this->~Breakpoint();
        kind = other.kind;
        if (kind == Kind::Label)
            label = other.label;
        else
        {
            new (&src.file) std::string(std::move(other.src.file));
            src.line = other.src.line;
        }
    }
    return *this;
}

Breakpoint::~Breakpoint()
{
    if (kind == Kind::SrcLine)
    {
        using std::string;
        src.file.~string();
    }
}

il::support::Symbol DebugCtrl::internLabel(std::string_view label)
{
    return interner_.intern(label);
}

void DebugCtrl::addBreak(il::support::Symbol sym)
{
    if (sym)
        breaks_.emplace_back(sym);
}

bool DebugCtrl::shouldBreak(const il::core::BasicBlock &blk) const
{
    il::support::Symbol sym = interner_.intern(blk.label);
    for (const auto &b : breaks_)
        if (b.kind == Breakpoint::Kind::Label && b.label == sym)
            return true;
    return false;
}

void DebugCtrl::addBreak(std::string file, int line)
{
    breaks_.emplace_back(std::move(file), line);
}

bool DebugCtrl::shouldBreak(const il::support::SourceLoc &loc)
{
    if (!loc.isValid() || !sm_)
        return false;
    std::string_view path = sm_->getPath(loc.file_id);
    for (auto it = breaks_.begin(); it != breaks_.end(); ++it)
        if (it->kind == Breakpoint::Kind::SrcLine && it->src.line == static_cast<int>(loc.line) &&
            it->src.file == path)
        {
            breaks_.erase(it);
            return true;
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

} // namespace il::vm
