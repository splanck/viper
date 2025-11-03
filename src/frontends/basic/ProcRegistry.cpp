//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the registry that tracks BASIC procedure declarations, ensuring
// unique names and emitting diagnostics when conflicts occur.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Procedure registry implementation for the BASIC semantic analyser.
/// @details Maintains a hash table of function/subroutine signatures and exposes
///          helpers for registering new declarations, clearing state, and
///          performing lookups.

#include "frontends/basic/ProcRegistry.hpp"

#include <unordered_set>
#include <utility>

namespace il::frontends::basic
{

/// @brief Construct a registry that records diagnostics through @p d.
ProcRegistry::ProcRegistry(SemanticDiagnostics &d) : de(d) {}

/// @brief Remove all procedures registered so far.
/// @details Clears the internal table so a new compilation unit can start with a
///          clean namespace.
void ProcRegistry::clear()
{
    procs_.clear();
}

/// @brief Build a canonical signature from a descriptor collected during analysis.
///
/// The helper copies declaration metadata into a stable signature, performs
/// duplicate parameter checks, and validates array parameter types against the
/// BASIC specification.
///
/// @param descriptor Source-level procedure description.
/// @return Populated signature describing the procedure for later lookup.
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
            de.emit(diag::BasicDiag::DuplicateParameter,
                    p.loc,
                    static_cast<uint32_t>(p.name.size()),
                    std::initializer_list<diag::Replacement>{diag::Replacement{"name", p.name}});
        }
        if (p.is_array && p.type != Type::I64 && p.type != Type::Str)
        {
            de.emit(diag::BasicDiag::ArrayParamType,
                    p.loc,
                    static_cast<uint32_t>(p.name.size()));
        }
        sig.params.push_back({p.type, p.is_array});
    }

    return sig;
}

/// @brief Register a procedure using the shared descriptor implementation.
///
/// Emits diagnostics when duplicate declarations are discovered; otherwise the
/// signature is stored for later lookup.
///
/// @param name Name of the procedure to register.
/// @param descriptor Metadata describing the procedure signature.
/// @param loc Source location of the declaration for diagnostics.
void ProcRegistry::registerProcImpl(std::string_view name,
                                    const ProcDescriptor &descriptor,
                                    il::support::SourceLoc loc)
{
    std::string nameStr{name};

    if (procs_.count(nameStr))
    {
        de.emit(diag::BasicDiag::DuplicateProcedure,
                loc,
                static_cast<uint32_t>(nameStr.size()),
                std::initializer_list<diag::Replacement>{diag::Replacement{"name", nameStr}});
        return;
    }

    procs_.emplace(std::move(nameStr), buildSignature(descriptor));
}

/// @brief Register a FUNCTION declaration with its return type and parameters.
/// @details Constructs a @ref ProcDescriptor capturing the declaration metadata
///          before delegating to @ref registerProcImpl.
void ProcRegistry::registerProc(const FunctionDecl &f)
{
    const ProcDescriptor descriptor{
        ProcSignature::Kind::Function, f.ret, std::span<const Param>{f.params}, f.loc};
    registerProcImpl(f.name, descriptor, f.loc);
}

/// @brief Register a SUB declaration with its parameter list.
/// @details Functions similarly to @ref registerProc for functions but records a
///          void return type.
void ProcRegistry::registerProc(const SubDecl &s)
{
    const ProcDescriptor descriptor{
        ProcSignature::Kind::Sub, std::nullopt, std::span<const Param>{s.params}, s.loc};
    registerProcImpl(s.name, descriptor, s.loc);
}

/// @brief Access the internal procedure table for iteration.
const ProcTable &ProcRegistry::procs() const
{
    return procs_;
}

/// @brief Look up a registered procedure by name.
///
/// @param name Identifier to search for.
/// @return Pointer to the stored signature when found; otherwise nullptr.
const ProcSignature *ProcRegistry::lookup(const std::string &name) const
{
    auto it = procs_.find(name);
    return it == procs_.end() ? nullptr : &it->second;
}

} // namespace il::frontends::basic
