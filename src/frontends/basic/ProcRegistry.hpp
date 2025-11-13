// File: src/frontends/basic/ProcRegistry.hpp
// Purpose: Manages BASIC procedure signatures and registration diagnostics.
// Key invariants: Each procedure name maps to a unique signature.
// Ownership/Lifetime: Borrows SemanticDiagnostics; no AST ownership.
// Links: docs/codemap.md
#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "frontends/basic/SemanticDiagnostics.hpp"
#include "frontends/basic/ast/DeclNodes.hpp"

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
    explicit ProcRegistry(SemanticDiagnostics &d);

    void clear();

    struct ProcEntry
    {
        const void *node{nullptr};
        il::support::SourceLoc loc{};
    };

    void registerProc(const FunctionDecl &f);

    void registerProc(const SubDecl &s);

    const ProcTable &procs() const;

    const ProcSignature *lookup(const std::string &name) const;

    // P1.3 API additions
    void AddProc(const FunctionDecl *fn, il::support::SourceLoc loc);
    const ProcEntry *LookupExact(std::string_view qualified) const;

  private:
    struct ProcDescriptor
    {
        ProcSignature::Kind kind;
        std::optional<Type> retType;
        std::span<const Param> params;
        il::support::SourceLoc loc;
    };

    ProcSignature buildSignature(const ProcDescriptor &descriptor);

    void registerProcImpl(std::string_view name,
                          const ProcDescriptor &descriptor,
                          il::support::SourceLoc loc);

    SemanticDiagnostics &de;
    ProcTable procs_;
    std::unordered_map<std::string, ProcEntry> byQualified_;
};

} // namespace il::frontends::basic
