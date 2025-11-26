//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/BasicSymbolQuery.cpp
// Purpose: Implements the BasicSymbolQuery facade for symbol/type lookups.
// Key invariants: All methods delegate to existing Lowerer/SemanticAnalyzer
//                 APIs without caching; no state mutation.
// Ownership/Lifetime: Stateless facade; relies on referenced objects.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicSymbolQuery.hpp"

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"

namespace il::frontends::basic
{

BasicSymbolQuery::BasicSymbolQuery(const Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

// =============================================================================
// Module-Level Queries
// =============================================================================

bool BasicSymbolQuery::isModuleLevelGlobal(std::string_view name) const
{
    const auto *sema = lowerer_.semanticAnalyzer();
    if (!sema)
        return false;
    return sema->isModuleLevelSymbol(std::string(name));
}

bool BasicSymbolQuery::isCrossProcGlobal(std::string_view name) const
{
    return lowerer_.isCrossProcGlobal(std::string(name));
}

// =============================================================================
// Symbol Type Queries
// =============================================================================

bool BasicSymbolQuery::isSymbolArray(std::string_view name) const
{
    const auto *info = lowerer_.findSymbol(name);
    return info && info->isArray;
}

bool BasicSymbolQuery::isSymbolObject(std::string_view name) const
{
    const auto *info = lowerer_.findSymbol(name);
    return info && info->isObject;
}

bool BasicSymbolQuery::hasExplicitType(std::string_view name) const
{
    const auto *info = lowerer_.findSymbol(name);
    return info && info->hasType;
}

std::optional<Type> BasicSymbolQuery::getSymbolType(std::string_view name) const
{
    const auto *info = lowerer_.findSymbol(name);
    if (!info)
        return std::nullopt;
    return info->type;
}

std::optional<Type> BasicSymbolQuery::getArrayElementType(std::string_view name) const
{
    const auto *info = lowerer_.findSymbol(name);
    if (!info || !info->isArray)
        return std::nullopt;
    return info->type;
}

// =============================================================================
// Object/Class Queries
// =============================================================================

std::string BasicSymbolQuery::getObjectClassForSymbol(std::string_view name) const
{
    // Check symbol lookup first
    const auto *info = lowerer_.findSymbol(name);
    if (info && info->isObject && !info->objectClass.empty())
        return info->objectClass;

    // Check module-level scalar object cache
    std::string moduleClass = lowerer_.lookupModuleArrayElemClass(name);
    if (!moduleClass.empty())
        return moduleClass;

    return {};
}

std::string BasicSymbolQuery::getObjectArrayElementClass(std::string_view name) const
{
    return lowerer_.lookupModuleArrayElemClass(name);
}

// =============================================================================
// Field Scope Queries
// =============================================================================

bool BasicSymbolQuery::isFieldInScope(std::string_view name) const
{
    return lowerer_.isFieldInScope(name);
}

// =============================================================================
// Semantic Analyzer Delegation
// =============================================================================

std::optional<Type> BasicSymbolQuery::lookupInferredType(std::string_view name) const
{
    const auto *sema = lowerer_.semanticAnalyzer();
    if (!sema)
        return std::nullopt;

    auto semaType = sema->lookupVarType(std::string(name));
    if (!semaType)
        return std::nullopt;

    // Convert SemanticAnalyzer::Type to AST Type
    using SemaType = SemanticAnalyzer::Type;
    switch (*semaType)
    {
        case SemaType::Int:
            return Type::I64;
        case SemaType::Float:
            return Type::F64;
        case SemaType::String:
            return Type::Str;
        case SemaType::Bool:
            return Type::Bool;
        default:
            return std::nullopt;
    }
}

} // namespace il::frontends::basic
