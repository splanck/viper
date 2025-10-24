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

    void registerProc(const FunctionDecl &f);

    void registerProc(const SubDecl &s);

    const ProcTable &procs() const;

    const ProcSignature *lookup(const std::string &name) const;

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
};

} // namespace il::frontends::basic
