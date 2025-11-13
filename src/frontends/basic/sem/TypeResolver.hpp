// File: src/frontends/basic/sem/TypeResolver.hpp
// Purpose: Resolve type names using NamespaceRegistry and UsingContext with ambiguity detection.
// Key invariants:
//   - Case-insensitive type lookups.
//   - Ambiguity detection produces sorted, stable contender lists.
//   - Qualified names bypass USING; simple names use precedence rules.
// Ownership/Lifetime: TypeResolver does not own registry or context; caller ensures lifetime.
// Links: docs/codemap.md, CLAUDE.md

#pragma once

#include "frontends/basic/sem/NamespaceRegistry.hpp"
#include "frontends/basic/sem/UsingContext.hpp"
#include <string>
#include <vector>

namespace il::frontends::basic
{

/// @brief Resolves type names using namespace registry and using context.
///
/// @details Implements compile-time type resolution with the following precedence:
///          - Qualified names (containing '.') bypass USING imports
///          - Simple names walk up current namespace chain, then try USING imports
///          - Ambiguity is detected and reported with sorted contender list
///
/// @invariant All lookups are case-insensitive.
/// @invariant Ambiguity produces deterministic (sorted) contender lists.
class TypeResolver
{
  public:
    /// @brief Type kind discriminator.
    enum class Kind
    {
        Unknown,   ///< Type not found or ambiguous.
        Class,     ///< Resolved to a class type.
        Interface, ///< Resolved to an interface type.
    };

    /// @brief Result of type name resolution.
    struct Result
    {
        /// True if exactly one type was found; false if none or ambiguous.
        bool found{false};

        /// Fully-qualified canonical name if found; empty otherwise.
        std::string qname;

        /// Type kind if found; Unknown otherwise.
        Kind kind{Kind::Unknown};

        /// If ambiguous (found=false && !contenders.empty()), list of matching FQ names.
        /// Sorted case-insensitively for deterministic diagnostics.
        std::vector<std::string> contenders;
    };

    /// @brief Construct a resolver with registry and using context.
    /// @param ns NamespaceRegistry containing declared types.
    /// @param uc UsingContext containing file-scoped imports.
    TypeResolver(const NamespaceRegistry &ns, const UsingContext &uc);

    /// @brief Resolve a type name in the given namespace context.
    /// @details Implements the resolution algorithm:
    ///          1. If name contains '.':
    ///             - If first segment is an alias, expand and check existence
    ///             - Else treat as fully-qualified and check existence
    ///          2. If simple name:
    ///             - Walk up current namespace chain (A.B.C → A.B → A → global)
    ///             - Try USING imports in declaration order
    ///             - Return found/ambiguous/not-found
    /// @param name Type name to resolve (simple or qualified).
    /// @param currentNsChain Current namespace path segments (e.g., {"A", "B", "C"}).
    /// @return Result with found flag, qualified name, kind, and contenders if ambiguous.
    [[nodiscard]] Result resolve(std::string name,
                                 const std::vector<std::string> &currentNsChain) const;

  private:
    /// @brief Convert a string to lowercase for case-insensitive comparison.
    [[nodiscard]] static std::string toLower(const std::string &str);

    /// @brief Join namespace segments with '.' separator.
    [[nodiscard]] static std::string joinPath(const std::vector<std::string> &segments);

    /// @brief Split a dotted name into segments.
    [[nodiscard]] static std::vector<std::string> splitPath(const std::string &path);

    /// @brief Try to resolve name in a specific namespace.
    /// @return Fully-qualified name if found; empty otherwise.
    [[nodiscard]] std::string tryResolveInNamespace(const std::string &ns,
                                                    const std::string &typeName) const;

    /// @brief Convert NamespaceRegistry::TypeKind to TypeResolver::Kind.
    [[nodiscard]] static Kind convertKind(NamespaceRegistry::TypeKind nsk);

    const NamespaceRegistry &registry_;
    const UsingContext &using_;
};

} // namespace il::frontends::basic
