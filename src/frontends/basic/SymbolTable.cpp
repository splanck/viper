//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/SymbolTable.cpp
// Purpose: Implementation of unified symbol table abstraction.
//
// Key invariants:
//   - All symbol lookups check field scopes before the main symbol table
//   - String literals are preserved across procedure resets for deduplication
//   - Symbol names are stored as-is (case sensitivity handled by caller)
//
// Ownership/Lifetime: See SymbolTable.hpp
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/SymbolTable.hpp"

#include "frontends/basic/TypeSuffix.hpp"

namespace il::frontends::basic
{

// =============================================================================
// Core Symbol Operations
// =============================================================================

SymbolInfo &SymbolTable::define(std::string_view name)
{
    std::string key(name);
    auto [it, inserted] = symbols_.emplace(std::move(key), SymbolInfo{});
    if (inserted)
    {
        // Initialize with BASIC defaults
        it->second.type = AstType::I64;
        it->second.hasType = false;
        it->second.isArray = false;
        it->second.isBoolean = false;
        it->second.referenced = false;
        it->second.isObject = false;
        it->second.objectClass.clear();
        it->second.isStatic = false;
        it->second.isByRefParam = false;
    }
    return it->second;
}

SymbolInfo *SymbolTable::lookup(std::string_view name)
{
    // Check main symbol table first (heterogeneous lookup, no allocation)
    if (auto it = symbols_.find(name); it != symbols_.end())
        return &it->second;

    // Fall back to field scopes
    return lookupInFieldScopes(name);
}

const SymbolInfo *SymbolTable::lookup(std::string_view name) const
{
    // Heterogeneous lookup, no allocation
    if (auto it = symbols_.find(name); it != symbols_.end())
        return &it->second;

    return lookupInFieldScopes(name);
}

bool SymbolTable::contains(std::string_view name) const
{
    return lookup(name) != nullptr;
}

bool SymbolTable::remove(std::string_view name)
{
    // Heterogeneous lookup for erase
    auto it = symbols_.find(name);
    if (it != symbols_.end())
    {
        symbols_.erase(it);
        return true;
    }
    return false;
}

void SymbolTable::resetForNewProcedure()
{
    // Preserve symbols with string labels (for literal deduplication)
    for (auto it = symbols_.begin(); it != symbols_.end();)
    {
        SymbolInfo &info = it->second;
        if (!info.stringLabel.empty())
        {
            // Reset mutable state but keep the string label
            info.type = AstType::I64;
            info.hasType = false;
            info.isArray = false;
            info.isBoolean = false;
            info.referenced = false;
            info.isObject = false;
            info.objectClass.clear();
            info.slotId.reset();
            info.arrayLengthSlot.reset();
            info.isStatic = false;
            info.isByRefParam = false;
            ++it;
        }
        else
        {
            it = symbols_.erase(it);
        }
    }

    // Clear field scopes
    fieldScopes_.clear();
}

void SymbolTable::clear()
{
    symbols_.clear();
    fieldScopes_.clear();
}

// =============================================================================
// Type Operations
// =============================================================================

void SymbolTable::setType(std::string_view name, AstType type)
{
    auto &info = define(name);
    info.type = type;
    info.hasType = true;
    info.isBoolean = !info.isArray && type == AstType::Bool;
}

std::optional<SymbolTable::AstType> SymbolTable::getType(std::string_view name) const
{
    const auto *info = lookup(name);
    if (!info)
        return std::nullopt;
    return info->type;
}

bool SymbolTable::hasExplicitType(std::string_view name) const
{
    const auto *info = lookup(name);
    return info && info->hasType;
}

// =============================================================================
// Symbol Classification
// =============================================================================

void SymbolTable::markReferenced(std::string_view name, std::optional<AstType> inferredType)
{
    if (name.empty())
        return;

    auto &info = define(name);

    // Apply inferred type if symbol doesn't have an explicit type
    if (!info.hasType)
    {
        if (inferredType)
        {
            info.type = *inferredType;
        }
        else
        {
            // Fall back to suffix-based inference
            info.type = inferAstTypeFromName(name);
        }
        info.hasType = true;
        info.isBoolean = !info.isArray && info.type == AstType::Bool;
    }

    info.referenced = true;
}

void SymbolTable::markArray(std::string_view name)
{
    if (name.empty())
        return;

    auto &info = define(name);
    info.isArray = true;
    // Arrays are pointer-typed; clear boolean flag
    if (info.isBoolean)
        info.isBoolean = false;
}

void SymbolTable::markStatic(std::string_view name)
{
    if (name.empty())
        return;

    auto &info = define(name);
    info.isStatic = true;
}

void SymbolTable::markObject(std::string_view name, std::string className)
{
    if (name.empty())
        return;

    auto &info = define(name);
    info.isObject = true;
    info.objectClass = std::move(className);
    info.hasType = true;
}

void SymbolTable::markByRef(std::string_view name)
{
    if (name.empty())
        return;

    auto &info = define(name);
    info.isByRefParam = true;
}

// =============================================================================
// Symbol Query
// =============================================================================

bool SymbolTable::isArray(std::string_view name) const
{
    const auto *info = lookup(name);
    return info && info->isArray;
}

bool SymbolTable::isObject(std::string_view name) const
{
    const auto *info = lookup(name);
    return info && info->isObject;
}

bool SymbolTable::isStatic(std::string_view name) const
{
    const auto *info = lookup(name);
    return info && info->isStatic;
}

bool SymbolTable::isByRef(std::string_view name) const
{
    const auto *info = lookup(name);
    return info && info->isByRefParam;
}

bool SymbolTable::isReferenced(std::string_view name) const
{
    const auto *info = lookup(name);
    return info && info->referenced;
}

std::string SymbolTable::getObjectClass(std::string_view name) const
{
    const auto *info = lookup(name);
    if (!info || !info->isObject)
        return {};
    return info->objectClass;
}

// =============================================================================
// Slot Management
// =============================================================================

void SymbolTable::setSlotId(std::string_view name, unsigned slotId)
{
    auto &info = define(name);
    info.slotId = slotId;
}

std::optional<unsigned> SymbolTable::getSlotId(std::string_view name) const
{
    const auto *info = lookup(name);
    if (!info)
        return std::nullopt;
    return info->slotId;
}

void SymbolTable::setArrayLengthSlot(std::string_view name, unsigned slotId)
{
    auto &info = define(name);
    info.arrayLengthSlot = slotId;
}

std::optional<unsigned> SymbolTable::getArrayLengthSlot(std::string_view name) const
{
    const auto *info = lookup(name);
    if (!info)
        return std::nullopt;
    return info->arrayLengthSlot;
}

// =============================================================================
// String Literal Caching
// =============================================================================

void SymbolTable::setStringLabel(std::string_view name, std::string label)
{
    auto &info = define(name);
    info.stringLabel = std::move(label);
}

std::string SymbolTable::getStringLabel(std::string_view name) const
{
    const auto *info = lookup(name);
    if (!info)
        return {};
    return info->stringLabel;
}

bool SymbolTable::hasStringLabel(std::string_view name) const
{
    const auto *info = lookup(name);
    return info && !info->stringLabel.empty();
}

// =============================================================================
// Field Scope Management
// =============================================================================

void SymbolTable::pushFieldScope(const ClassLayout *layout)
{
    FieldScope scope;
    scope.layout = layout;

    if (layout)
    {
        // Populate field symbols from the class layout
        for (const auto &field : layout->fields)
        {
            SymbolInfo info;
            info.type = field.type;
            info.hasType = true;
            info.isArray = field.isArray;
            info.isBoolean = (field.type == AstType::Bool);
            info.referenced = false;
            info.isObject = !field.objectClassName.empty();
            info.objectClass = field.objectClassName;
            scope.symbols.emplace(field.name, std::move(info));
        }
    }

    fieldScopes_.push_back(std::move(scope));
}

void SymbolTable::popFieldScope()
{
    if (!fieldScopes_.empty())
        fieldScopes_.pop_back();
}

bool SymbolTable::isFieldInScope(std::string_view name) const
{
    if (name.empty())
        return false;

    // Heterogeneous lookup, no allocation
    for (auto it = fieldScopes_.rbegin(); it != fieldScopes_.rend(); ++it)
    {
        if (it->symbols.find(name) != it->symbols.end())
            return true;
    }
    return false;
}

const FieldScope *SymbolTable::activeFieldScope() const
{
    if (fieldScopes_.empty())
        return nullptr;
    return &fieldScopes_.back();
}

// =============================================================================
// Internal Helpers
// =============================================================================

SymbolInfo *SymbolTable::lookupInFieldScopes(std::string_view name)
{
    // Heterogeneous lookup, no allocation
    for (auto scopeIt = fieldScopes_.rbegin(); scopeIt != fieldScopes_.rend(); ++scopeIt)
    {
        auto symIt = scopeIt->symbols.find(name);
        if (symIt != scopeIt->symbols.end())
            return &symIt->second;
    }
    return nullptr;
}

const SymbolInfo *SymbolTable::lookupInFieldScopes(std::string_view name) const
{
    // Heterogeneous lookup, no allocation
    for (auto scopeIt = fieldScopes_.rbegin(); scopeIt != fieldScopes_.rend(); ++scopeIt)
    {
        auto symIt = scopeIt->symbols.find(name);
        if (symIt != scopeIt->symbols.end())
            return &symIt->second;
    }
    return nullptr;
}

} // namespace il::frontends::basic
