//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/sem/RegistryBuilder.cpp
// Purpose: Implements buildNamespaceRegistry to populate namespace registry.
// Key invariants:
//   - Walks AST in declaration order.
//   - Maintains nsStack for qualified name construction.
//   - Records USING directives in declaration order.
// Ownership/Lifetime: Does not own registry or usings.
// Links: docs/codemap.md, CLAUDE.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Registry builder for namespace and type declarations.
/// @details Provides the @ref buildNamespaceRegistry routine that populates
///          the registry by scanning the parsed BASIC program. The helper
///          copies relevant metadata from the AST into the registry, enabling
///          deterministic type resolution during semantic analysis.

#include "frontends/basic/sem/RegistryBuilder.hpp"
#include "frontends/basic/AST.hpp"
#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/sem/TypeRegistry.hpp"

#include <functional>
#include <string>
#include <vector>

namespace il::frontends::basic
{

/// @brief Populate NamespaceRegistry and UsingContext from a parsed BASIC program.
/// @details Clears any pre-existing entries then walks the top-level statements
///          collecting namespace declarations, class declarations, interface declarations,
///          and USING directives. Maintains a namespace stack to construct qualified names.
/// @param program Parsed BASIC program supplying declarations.
/// @param registry Registry instance that receives namespace and type metadata.
/// @param usings Context that receives USING directives.
/// @param emitter Optional diagnostics interface reserved for future checks.
void buildNamespaceRegistry(const Program &program,
                            NamespaceRegistry &registry,
                            UsingContext &usings,
                            DiagnosticEmitter *emitter)
{
    // Clear previous state.
    usings.clear();

    // Seed well-known runtime namespaces and built-in external types.
    // Catalog-only: registers namespaces and type names; members come later.
    // This also makes `USING Viper.System.*` resolvable when enabled.
    seedRuntimeTypeCatalog(registry);

    // Maintain namespace stack for qualified name construction.
    std::vector<std::string> nsStack;

    // Helper to join namespace segments.
    auto joinNs = [&]() -> std::string
    {
        if (nsStack.empty())
            return {};
        std::string result;
        std::size_t size = 0;
        for (const auto &s : nsStack)
            size += s.size() + 1;
        if (size)
            size -= 1; // trailing dot not needed
        result.reserve(size);
        for (std::size_t i = 0; i < nsStack.size(); ++i)
        {
            if (i)
                result.push_back('.');
            result += nsStack[i];
        }
        return result;
    };

    // Recursive lambda to scan statements and populate the registry.
    std::function<void(const std::vector<StmtPtr> &)> scan;
    scan = [&](const std::vector<StmtPtr> &stmts)
    {
        for (const auto &stmtPtr : stmts)
        {
            if (!stmtPtr)
                continue;

            switch (stmtPtr->stmtKind())
            {
                case Stmt::Kind::NamespaceDecl:
                {
                    const auto &ns = static_cast<const NamespaceDecl &>(*stmtPtr);
                    // Push segments onto stack.
                    for (const auto &seg : ns.path)
                        nsStack.push_back(seg);

                    // Register namespace.
                    std::string nsFull = joinNs();
                    if (!nsFull.empty())
                        registry.registerNamespace(nsFull);

                    // Recurse into body.
                    scan(ns.body);

                    // Pop segments.
                    nsStack.resize(
                        nsStack.size() >= ns.path.size() ? nsStack.size() - ns.path.size() : 0);
                    break;
                }

                case Stmt::Kind::ClassDecl:
                {
                    const auto &classDecl = static_cast<const ClassDecl &>(*stmtPtr);
                    std::string nsFull = joinNs();

                    // Register class in current namespace.
                    registry.registerClass(nsFull, classDecl.name);
                    break;
                }

                case Stmt::Kind::InterfaceDecl:
                {
                    const auto &ifaceDecl = static_cast<const InterfaceDecl &>(*stmtPtr);

                    // InterfaceDecl.qualifiedName contains full path including type name.
                    // Extract namespace (all but last segment) and name (last segment).
                    if (!ifaceDecl.qualifiedName.empty())
                    {
                        std::string ifaceName = ifaceDecl.qualifiedName.back();
                        std::string nsFull;
                        if (ifaceDecl.qualifiedName.size() > 1)
                        {
                            // Build namespace from all but last segment.
                            for (std::size_t i = 0; i + 1 < ifaceDecl.qualifiedName.size(); ++i)
                            {
                                if (i > 0)
                                    nsFull.push_back('.');
                                nsFull += ifaceDecl.qualifiedName[i];
                            }
                        }

                        // Register interface in its namespace.
                        registry.registerInterface(nsFull, ifaceName);
                    }
                    break;
                }

                case Stmt::Kind::UsingDecl:
                {
                    const auto &usingDecl = static_cast<const UsingDecl &>(*stmtPtr);

                    // Build dotted namespace path from segments.
                    std::string nsPath;
                    for (std::size_t i = 0; i < usingDecl.namespacePath.size(); ++i)
                    {
                        if (i)
                            nsPath.push_back('.');
                        nsPath += usingDecl.namespacePath[i];
                    }

                    // Record only file-scoped USING directives in UsingContext.
                    // USING inside NAMESPACE blocks is handled by SemanticAnalyzer's
                    // scoped usingStack_ and must not leak into file-scoped context.
                    if (nsStack.empty() && !nsPath.empty())
                        usings.add(std::move(nsPath), usingDecl.alias, usingDecl.loc);
                    break;
                }

                default:
                    break;
            }
        }
    };

    scan(program.main);
}

} // namespace il::frontends::basic
