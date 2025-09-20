// File: src/frontends/basic/ProcRegistry.cpp
// Purpose: Implements BASIC procedure registry behaviors and diagnostics.
// Key invariants: Registry maintains unique procedure names and signatures.
// Ownership/Lifetime: ProcRegistry borrows SemanticDiagnostics lifetime.
// Links: docs/class-catalog.md

#include "frontends/basic/ProcRegistry.hpp"

#include <unordered_set>
#include <utility>

namespace il::frontends::basic
{

ProcRegistry::ProcRegistry(SemanticDiagnostics &d) : de(d) {}

void ProcRegistry::clear()
{
    procs_.clear();
}

void ProcRegistry::registerProc(const FunctionDecl &f)
{
    if (procs_.count(f.name))
    {
        std::string msg = "duplicate procedure '" + f.name + "'";
        de.emit(il::support::Severity::Error,
                "B1004",
                f.loc,
                static_cast<uint32_t>(f.name.size()),
                std::move(msg));
        return;
    }
    ProcSignature sig;
    sig.kind = ProcSignature::Kind::Function;
    sig.retType = f.ret;
    std::unordered_set<std::string> paramNames;
    for (const auto &p : f.params)
    {
        if (!paramNames.insert(p.name).second)
        {
            std::string msg = "duplicate parameter '" + p.name + "'";
            de.emit(il::support::Severity::Error,
                    "B1005",
                    p.loc,
                    static_cast<uint32_t>(p.name.size()),
                    std::move(msg));
        }
        if (p.is_array && p.type != Type::I64 && p.type != Type::Str && p.type != Type::Bool)
        {
            std::string msg = "array parameter must be i64 or str";
            de.emit(il::support::Severity::Error,
                    "B2004",
                    p.loc,
                    static_cast<uint32_t>(p.name.size()),
                    std::move(msg));
        }
        sig.params.push_back({p.type, p.is_array});
    }
    procs_.emplace(f.name, std::move(sig));
}

void ProcRegistry::registerProc(const SubDecl &s)
{
    if (procs_.count(s.name))
    {
        std::string msg = "duplicate procedure '" + s.name + "'";
        de.emit(il::support::Severity::Error,
                "B1004",
                s.loc,
                static_cast<uint32_t>(s.name.size()),
                std::move(msg));
        return;
    }
    ProcSignature sig;
    sig.kind = ProcSignature::Kind::Sub;
    sig.retType = std::nullopt;
    std::unordered_set<std::string> paramNames;
    for (const auto &p : s.params)
    {
        if (!paramNames.insert(p.name).second)
        {
            std::string msg = "duplicate parameter '" + p.name + "'";
            de.emit(il::support::Severity::Error,
                    "B1005",
                    p.loc,
                    static_cast<uint32_t>(p.name.size()),
                    std::move(msg));
        }
        if (p.is_array && p.type != Type::I64 && p.type != Type::Str && p.type != Type::Bool)
        {
            std::string msg = "array parameter must be i64 or str";
            de.emit(il::support::Severity::Error,
                    "B2004",
                    p.loc,
                    static_cast<uint32_t>(p.name.size()),
                    std::move(msg));
        }
        sig.params.push_back({p.type, p.is_array});
    }
    procs_.emplace(s.name, std::move(sig));
}

const ProcTable &ProcRegistry::procs() const
{
    return procs_;
}

const ProcSignature *ProcRegistry::lookup(const std::string &name) const
{
    auto it = procs_.find(name);
    return it == procs_.end() ? nullptr : &it->second;
}

} // namespace il::frontends::basic
