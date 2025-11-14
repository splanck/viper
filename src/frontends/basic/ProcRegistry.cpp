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
#include "frontends/basic/Diag.hpp"
#include "frontends/basic/IdentifierUtil.hpp"

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
    byQualified_.clear();
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
            de.emit(diag::BasicDiag::ArrayParamType, p.loc, static_cast<uint32_t>(p.name.size()));
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
static std::string stripSuffix(std::string_view name)
{
    if (name.empty())
        return std::string{};
    char last = name.back();
    if (last == '$' || last == '#' || last == '!' || last == '&' || last == '%')
        name = name.substr(0, name.size() - 1);
    return std::string{name};
}

static std::string canonicalizeQualifiedFlat(std::string_view dotted)
{
    // Split on '.' and canonicalize each segment (ASCII lowercase).
    // For the final segment only, strip BASIC type suffix before canonicalization.
    std::vector<std::string> parts;
    parts.reserve(4);
    std::string segment;
    std::vector<std::string> raw;
    for (size_t i = 0; i <= dotted.size(); ++i)
    {
        if (i == dotted.size() || dotted[i] == '.')
        {
            raw.emplace_back(std::move(segment));
            segment.clear();
        }
        else
        {
            segment.push_back(dotted[i]);
        }
    }

    parts.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i)
    {
        std::string_view seg = raw[i];
        if (seg.empty())
        {
            parts.emplace_back(std::string{});
            continue;
        }
        // Strip type suffix from the final identifier segment, if present.
        if (i + 1 == raw.size())
        {
            char last = seg.back();
            if (last == '$' || last == '#' || last == '!' || last == '&' || last == '%')
            {
                seg = seg.substr(0, seg.size() - 1);
            }
        }
        std::string canon = CanonicalizeIdent(seg);
        if (canon.empty() && !seg.empty())
        {
            // Invalid character encountered; signal failure.
            return std::string{};
        }
        parts.emplace_back(std::move(canon));
    }
    return JoinQualified(parts);
}

void ProcRegistry::registerProcImpl(std::string_view name,
                                    const ProcDescriptor &descriptor,
                                    il::support::SourceLoc loc)
{
    // Derive canonical qualified key. Lowercase all segments and strip suffix
    // from the final segment for unqualified or dotted names alike.
    std::string key;
    if (name.find('.') != std::string_view::npos)
        key = canonicalizeQualifiedFlat(name);
    else
        key = CanonicalizeIdent(stripSuffix(name));

    if (key.empty())
    {
        // Fallback to original for error text, but avoid inserting.
        std::string nameStr{name};
        de.emit(diag::BasicDiag::DuplicateProcedure,
                loc,
                static_cast<uint32_t>(nameStr.size()),
                std::initializer_list<diag::Replacement>{diag::Replacement{"name", nameStr}});
        return;
    }

    auto it = byQualified_.find(key);
    if (it != byQualified_.end())
    {
        // Duplicate: emit using canonical lowercase to match diagnostics expectations.
        std::string display = key;
        diagx::ErrorDuplicateProc(de.emitter(), display, it->second.loc, loc);
        return;
    }

    byQualified_.emplace(key, ProcEntry{nullptr, loc});
    // Build signature once, then insert under both original and canonical keys for lookup.
    ProcSignature sig = buildSignature(descriptor);
    std::string nameStr{name};
    procs_.emplace(std::move(nameStr), sig);
    procs_.emplace(key, sig);
}

/// @brief Register a FUNCTION declaration with its return type and parameters.
/// @details Constructs a @ref ProcDescriptor capturing the declaration metadata
///          before delegating to @ref registerProcImpl.
void ProcRegistry::registerProc(const FunctionDecl &f)
{
    const ProcDescriptor descriptor{
        ProcSignature::Kind::Function, f.ret, std::span<const Param>{f.params}, f.loc};
    std::string_view nm =
        f.qualifiedName.empty() ? std::string_view{f.name} : std::string_view{f.qualifiedName};
    registerProcImpl(nm, descriptor, f.loc);
}

/// @brief Register a SUB declaration with its parameter list.
/// @details Functions similarly to @ref registerProc for functions but records a
///          void return type.
void ProcRegistry::registerProc(const SubDecl &s)
{
    const ProcDescriptor descriptor{
        ProcSignature::Kind::Sub, std::nullopt, std::span<const Param>{s.params}, s.loc};
    std::string_view nm =
        s.qualifiedName.empty() ? std::string_view{s.name} : std::string_view{s.qualifiedName};
    registerProcImpl(nm, descriptor, s.loc);
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
    // First try exact lookup for performance
    auto it = procs_.find(name);
    if (it != procs_.end())
        return &it->second;

    // Canonicalize qualified names (case-insensitive, strip suffix from final segment)
    std::string key;
    if (name.find('.') != std::string::npos)
        key = canonicalizeQualifiedFlat(name);
    else
        key = CanonicalizeIdent(stripSuffix(name));

    if (key.empty())
        return nullptr;

    it = procs_.find(key);
    return it == procs_.end() ? nullptr : &it->second;
}

void ProcRegistry::AddProc(const FunctionDecl *fn, il::support::SourceLoc loc)
{
    if (!fn)
        return;
    const ProcDescriptor descriptor{
        ProcSignature::Kind::Function, fn->ret, std::span<const Param>{fn->params}, loc};
    std::string_view nm = fn->qualifiedName.empty() ? std::string_view{fn->name}
                                                    : std::string_view{fn->qualifiedName};
    registerProcImpl(nm, descriptor, loc);
}

const ProcRegistry::ProcEntry *ProcRegistry::LookupExact(std::string_view qualified) const
{
    auto it = byQualified_.find(std::string{qualified});
    return it == byQualified_.end() ? nullptr : &it->second;
}

} // namespace il::frontends::basic
