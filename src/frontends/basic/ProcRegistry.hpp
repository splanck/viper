// File: src/frontends/basic/ProcRegistry.hpp
// Purpose: Manages BASIC procedure signatures and registration diagnostics.
// Key invariants: Each procedure name maps to a unique signature.
// Ownership/Lifetime: Borrows SemanticDiagnostics; no AST ownership.
// Links: docs/class-catalog.md
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "frontends/basic/AST.hpp"
#include "frontends/basic/SemanticDiagnostics.hpp"

namespace il::frontends::basic
{

struct ProcSignature
{
    enum class Kind
    {
        Function,
        Sub
    } kind{Kind::Function};
    std::optional<Type> retType;

    struct Param
    {
        Type type{Type::I64};
        bool is_array{false};
    };

    std::vector<Param> params;
};

using ProcTable = std::unordered_map<std::string, ProcSignature>;

class ProcRegistry
{
  public:
    explicit ProcRegistry(SemanticDiagnostics &d) : de(d) {}

    void clear()
    {
        procs_.clear();
    }

    void registerProc(const FunctionDecl &f)
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
            if (p.is_array && p.type != Type::I64 && p.type != Type::Str)
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

    void registerProc(const SubDecl &s)
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
            if (p.is_array && p.type != Type::I64 && p.type != Type::Str)
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

    const ProcTable &procs() const
    {
        return procs_;
    }

    const ProcSignature *lookup(const std::string &name) const
    {
        auto it = procs_.find(name);
        return it == procs_.end() ? nullptr : &it->second;
    }

  private:
    SemanticDiagnostics &de;
    ProcTable procs_;
};

} // namespace il::frontends::basic
