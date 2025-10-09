//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the procedure registry used by the BASIC front end to track
// declared functions and subs, enforce uniqueness, and capture diagnostic
// details for semantic analysis.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/ProcRegistry.hpp"

#include <unordered_set>
#include <utility>

namespace il::frontends::basic
{

/// @brief Construct a registry that reports errors through @p d.
ProcRegistry::ProcRegistry(SemanticDiagnostics &d) : de(d) {}

/// @brief Remove all recorded procedures from the registry.
void ProcRegistry::clear()
{
    procs_.clear();
}

/// @brief Build a canonical signature from a parsed procedure descriptor.
///
/// Validates the descriptor by checking for duplicate parameter names and
/// ensuring array parameters use supported element types.  Diagnostics are
/// emitted through the configured sink when violations occur.
///
/// @param descriptor Procedure metadata gathered during parsing.
/// @return Populated signature containing kind, return type, and parameter info.
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

    return sig;
}

/// @brief Register a procedure implementation after validating its signature.
///
/// Rejects duplicate names with a diagnostic and otherwise inserts the
/// descriptor's signature into the registry.
///
/// @param name       Procedure name to register.
/// @param descriptor Parsed procedure description.
/// @param loc        Source location used for diagnostics.
void ProcRegistry::registerProcImpl(std::string_view name,
                                    const ProcDescriptor &descriptor,
                                    il::support::SourceLoc loc)
{
    std::string nameStr{name};

    if (procs_.count(nameStr))
    {
        std::string msg = "duplicate procedure '" + nameStr + "'";
        de.emit(il::support::Severity::Error,
                "B1004",
                loc,
                static_cast<uint32_t>(nameStr.size()),
                std::move(msg));
        return;
    }

    procs_.emplace(std::move(nameStr), buildSignature(descriptor));
}

/// @brief Register a function declaration with the registry.
/// @param f Function declaration emitted by the parser.
void ProcRegistry::registerProc(const FunctionDecl &f)
{
    const ProcDescriptor descriptor{ProcSignature::Kind::Function,
                                    f.ret,
                                    std::span<const Param>{f.params},
                                    f.loc};
    registerProcImpl(f.name, descriptor, f.loc);
}

/// @brief Register a subroutine declaration with the registry.
/// @param s Sub declaration emitted by the parser.
void ProcRegistry::registerProc(const SubDecl &s)
{
    const ProcDescriptor descriptor{ProcSignature::Kind::Sub,
                                    std::nullopt,
                                    std::span<const Param>{s.params},
                                    s.loc};
    registerProcImpl(s.name, descriptor, s.loc);
}

/// @brief Access the internal map of registered procedures.
/// @return Reference to the registry's procedure table.
const ProcTable &ProcRegistry::procs() const
{
    return procs_;
}

/// @brief Retrieve the signature associated with a procedure name.
/// @param name Identifier of the procedure to look up.
/// @return Pointer to the signature when registered; nullptr otherwise.
const ProcSignature *ProcRegistry::lookup(const std::string &name) const
{
    auto it = procs_.find(name);
    return it == procs_.end() ? nullptr : &it->second;
}

} // namespace il::frontends::basic
