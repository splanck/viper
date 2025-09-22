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

ProcSignature ProcRegistry::buildSignature(const ProcDescriptor &descriptor)
{
    ProcSignature sig;
    sig.kind = descriptor.kind;
    sig.retType = descriptor.retType;

    std::unordered_set<std::string> paramNames;
    for (const auto &p : descriptor.params)
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

    return sig;
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
    const ProcDescriptor descriptor{ProcSignature::Kind::Function,
                                    f.ret,
                                    std::span<const Param>{f.params},
                                    f.loc};
    procs_.emplace(f.name, buildSignature(descriptor));
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
    const ProcDescriptor descriptor{ProcSignature::Kind::Sub,
                                    std::nullopt,
                                    std::span<const Param>{s.params},
                                    s.loc};
    procs_.emplace(s.name, buildSignature(descriptor));
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
