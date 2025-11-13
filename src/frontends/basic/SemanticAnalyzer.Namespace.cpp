//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/SemanticAnalyzer.Namespace.cpp
// Purpose: Implements namespace semantic checking including USING placement
//          and reserved-root validation.
// Key invariants:
//   - USING applies at file scope or inside NAMESPACE blocks; not inside procedures.
//   - "Viper" root namespace is reserved.
//   - Alias names cannot duplicate existing aliases or namespace names.
// Ownership/Lifetime: Borrows DiagnosticEmitter; no AST ownership.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Namespace semantic checking for BASIC frontend.
/// @details Implements validation rules for USING directives, namespace
///          declarations, and reserved-root enforcement per Track A spec.

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/IdentifierUtil.hpp"
#include "frontends/basic/SemanticAnalyzer.Internal.hpp"
#include <algorithm>
#include <cctype>

namespace il::frontends::basic
{

/// @brief Case-insensitive string comparison.
static bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    return std::equal(a.begin(),
                      a.end(),
                      b.begin(),
                      [](char ca, char cb)
                      {
                          return std::tolower(static_cast<unsigned char>(ca)) ==
                                 std::tolower(static_cast<unsigned char>(cb));
                      });
}

/// @brief Analyze namespace declaration.
/// @details Sets sawDecl_ to true to enforce USING placement rules.
///          Also maintains nsStack_ for nested namespace tracking.
void SemanticAnalyzer::analyzeNamespaceDecl(NamespaceDecl &decl)
{
    // Check for reserved "Viper" root namespace.
    if (!decl.path.empty() && iequals(decl.path[0], "Viper"))
    {
        de.emit(diag::BasicDiag::NsReservedViper, decl.loc, 1, {});
        return;
    }

    sawDecl_ = true;

    // Push namespace segments onto stack.
    for (const auto &seg : decl.path)
        nsStack_.push_back(seg);

    // Inherit USING context from parent scope and then apply local USINGs.
    UsingScope child;
    if (!usingStack_.empty())
    {
        // Inherit imports; aliases can be shadowed so keep local alias map empty.
        child.imports = usingStack_.back().imports;
    }
    usingStack_.push_back(std::move(child));

    // First pass: apply USING directives in this namespace to the current using scope.
    for (const auto &stmt : decl.body)
    {
        if (!stmt)
            continue;
        if (stmt->stmtKind() == Stmt::Kind::UsingDecl)
        {
            analyzeUsingDecl(static_cast<UsingDecl &>(*stmt));
        }
    }

    // Second pass: analyze remaining statements (children see combined parent + local using).
    for (const auto &stmt : decl.body)
    {
        if (!stmt)
            continue;
        if (stmt->stmtKind() != Stmt::Kind::UsingDecl)
            visitStmt(*stmt);
    }

    // Pop segments.
    size_t popCount = decl.path.size();
    if (nsStack_.size() >= popCount)
        nsStack_.resize(nsStack_.size() - popCount);

    // Pop USING scope.
    if (!usingStack_.empty())
        usingStack_.pop_back();
}

/// @brief Analyze class declaration.
/// @details Sets sawDecl_ to true and resolves base type and implemented interfaces.
void SemanticAnalyzer::analyzeClassDecl(ClassDecl &decl)
{
    sawDecl_ = true;

    // Resolve base class if present.
    if (decl.baseName)
    {
        std::string resolvedBase = resolveTypeRef(
            *decl.baseName, nsStack_, decl.loc, static_cast<uint32_t>(decl.baseName->size()));
        // Store resolved base for later use (error recovery - continue even if unresolved).
    }

    // Resolve implemented interfaces.
    for (const auto &ifaceQN : decl.implementsQualifiedNames)
    {
        // Build dotted name from qualified segments.
        std::string ifaceName;
        for (size_t i = 0; i < ifaceQN.size(); ++i)
        {
            if (i > 0)
                ifaceName.push_back('.');
            ifaceName += ifaceQN[i];
        }

        if (!ifaceName.empty())
        {
            std::string resolvedIface = resolveTypeRef(
                ifaceName, nsStack_, decl.loc, static_cast<uint32_t>(ifaceName.size()));
            // Store resolved interface for later use (error recovery - continue even if
            // unresolved).
        }
    }
}

/// @brief Analyze interface declaration.
/// @details Sets sawDecl_ to true to enforce USING placement rules.
void SemanticAnalyzer::analyzeInterfaceDecl(InterfaceDecl &decl)
{
    sawDecl_ = true;
}

/// @brief Analyze USING directive with full validation.
/// @details Enforces:
///          - USING cannot appear inside procedure bodies.
///          - Referenced namespace must exist.
///          - Aliases must be unique and not shadow namespaces.
///          - "Viper" root is reserved.
void SemanticAnalyzer::analyzeUsingDecl(UsingDecl &decl)
{
    // Disallow inside procedures (Phase 2 rule).
    if (activeProcScope_ != nullptr)
    {
        // Reuse existing diagnostic category for placement; message text may refer to scope.
        de.emit(diag::BasicDiag::NsUsingNotFileScope, decl.loc, 1, {});
        return;
    }

    // Build canonical lowercase namespace path from segments.
    std::string nsPath;
    if (!decl.namespacePath.empty())
        nsPath = CanonJoin(decl.namespacePath);

    if (nsPath.empty())
        return;

    // E_NS_009: Check for reserved "Viper" root namespace.
    if (!decl.namespacePath.empty() && iequals(decl.namespacePath[0], "Viper"))
    {
        de.emit(diag::BasicDiag::NsReservedViper, decl.loc, 1, {});
        return;
    }

    // E_NS_001: Namespace must exist in registry.
    if (!nsPath.empty())
    {
        if (!ns_.namespaceExists(nsPath))
        {
            // P2.4: Optional early validation (best-effort). Emit a warning but do not halt.
            std::string msg = std::string("unknown namespace '") + nsPath + "' in USING";
            de.emit(il::support::Severity::Warning, "W_NS_USING_UNKNOWN", decl.loc, 1, std::move(msg));
        }
    }

    // Validate alias if present.
    if (!usingStack_.empty())
    {
        UsingScope &cur = usingStack_.back();

        if (!decl.alias.empty())
        {
            // Canonicalize alias to lowercase.
            std::string aliasLower = Canon(decl.alias);
            if (aliasLower.find('.') != std::string::npos)
            {
                // Should not happen (parser enforces single identifier).
                de.emit(diag::BasicDiag::NsDuplicateAlias,
                        decl.loc,
                        1,
                        {{diag::Replacement{"alias", decl.alias}}});
                return;
            }

            // E_NS_004: Duplicate alias in the same scope is not allowed unless
            // it refers to the same target namespace (idempotent seeding).
            if (auto itDup = cur.aliases.find(aliasLower); itDup != cur.aliases.end())
            {
                if (itDup->second == nsPath)
                {
                    // Ignore duplicate that maps to the same target.
                    return;
                }
                de.emit(diag::BasicDiag::NsDuplicateAlias,
                        decl.loc,
                        1,
                        {{diag::Replacement{"alias", decl.alias}}});
                return;
            }

            // E_NS_007: Alias must not shadow a namespace name (only check exact alias).
            if (ns_.namespaceExists(aliasLower))
            {
                de.emit(diag::BasicDiag::NsAliasShadowsNs,
                        decl.loc,
                        1,
                        {{diag::Replacement{"alias", decl.alias}}});
                return;
            }

            cur.aliases.emplace(aliasLower, nsPath);
            cur.aliasLoc.emplace(aliasLower, decl.loc);
        }
        else
        {
            if (!nsPath.empty())
                cur.imports.insert(nsPath);
        }
    }
}

/// @brief Resolve a type reference and emit diagnostics if not found.
/// @details Uses TypeResolver to resolve the type name and emits:
///          - E_NS_002: Fully-qualified namespace exists but type is missing
///          - E_NS_003: Ambiguous type with sorted contenders
///          - E_NS_006: Type not found
/// @param typeName Type name to resolve (simple or qualified).
/// @param currentNsChain Current namespace context (segments).
/// @param loc Source location for diagnostics.
/// @param length Source length for diagnostics.
/// @return Fully-qualified type name if resolved; empty string on error.
std::string SemanticAnalyzer::resolveTypeRef(const std::string &typeName,
                                             const std::vector<std::string> &currentNsChain,
                                             il::support::SourceLoc loc,
                                             uint32_t length)
{
    if (!resolver_)
        return ""; // TypeResolver not initialized yet

    // Use TypeResolver to resolve the type.
    auto result = resolver_->resolve(typeName, currentNsChain);

    if (result.found)
    {
        // Successfully resolved.
        return result.qname;
    }

    // Not found - check if it's a qualified name with existing namespace.
    bool isQualified = typeName.find('.') != std::string::npos;

    if (isQualified && !result.contenders.empty())
    {
        // E_NS_003: Ambiguous type with multiple contenders.
        std::string candidates;
        for (size_t i = 0; i < result.contenders.size(); ++i)
        {
            if (i > 0)
                candidates += ", ";
            candidates += result.contenders[i];
        }

        de.emit(
            diag::BasicDiag::NsAmbiguousType,
            loc,
            length,
            {{diag::Replacement{"type", typeName}}, {diag::Replacement{"candidates", candidates}}});
        return "";
    }

    if (isQualified)
    {
        // Check if namespace exists but type is missing (E_NS_002).
        // Split the qualified name to get namespace and type parts.
        size_t lastDot = typeName.rfind('.');
        if (lastDot != std::string::npos)
        {
            std::string nsPath = typeName.substr(0, lastDot);
            std::string typeOnly = typeName.substr(lastDot + 1);

            if (ns_.namespaceExists(nsPath))
            {
                // E_NS_002: Namespace exists but doesn't contain the type.
                de.emit(diag::BasicDiag::NsTypeNotInNs,
                        loc,
                        length,
                        {{diag::Replacement{"ns", nsPath}}, {diag::Replacement{"type", typeOnly}}});
                return "";
            }
        }
    }

    // Check for ambiguity even with simple names.
    if (!result.contenders.empty())
    {
        // E_NS_003: Ambiguous type with multiple contenders.
        std::string candidates;
        for (size_t i = 0; i < result.contenders.size(); ++i)
        {
            if (i > 0)
                candidates += ", ";
            candidates += result.contenders[i];
        }

        de.emit(
            diag::BasicDiag::NsAmbiguousType,
            loc,
            length,
            {{diag::Replacement{"type", typeName}}, {diag::Replacement{"candidates", candidates}}});
        return "";
    }

    // E_NS_006: Type not found.
    de.emit(diag::BasicDiag::NsTypeNotFound, loc, length, {{diag::Replacement{"type", typeName}}});
    return "";
}

} // namespace il::frontends::basic
