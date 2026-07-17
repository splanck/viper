//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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
// Links: docs/internals/architecture.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/SymbolTable.hpp"

#include "frontends/basic/TypeSuffix.hpp"

#include <cctype>

namespace il::frontends::basic {
namespace {

/// @brief Produce the case-insensitive storage key for BASIC symbols.
/// @details Preserves BASIC type suffix characters while folding ASCII letters
///          to lowercase. This keeps `A$` distinct from `A` while making
///          `Counter` and `counter` resolve to the same symbol.
/// @param name Symbol spelling supplied by a caller.
/// @return Canonical map key.
std::string canonicalSymbolKey(std::string_view name) {
    std::string key;
    key.reserve(name.size());
    for (unsigned char ch : name)
        key.push_back(static_cast<char>(std::tolower(ch)));
    return key;
}

} // namespace

// =============================================================================
// Core Symbol Operations
// =============================================================================

/// @brief Get or create the symbol entry for @p name, initialized with BASIC defaults.
/// @return Reference to the (existing or newly inserted) symbol info.
SymbolInfo &SymbolTable::define(std::string_view name) {
    std::string key = canonicalSymbolKey(name);
    auto [it, inserted] = symbols_.emplace(std::move(key), SymbolInfo{});
    if (inserted) {
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

/// @brief Look up a symbol (mutable), checking the main table then field scopes.
/// @return Pointer to the symbol info, or nullptr if undefined.
SymbolInfo *SymbolTable::lookup(std::string_view name) {
    // Check main symbol table first (heterogeneous lookup, no allocation)
    const std::string key = canonicalSymbolKey(name);
    if (auto it = symbols_.find(key); it != symbols_.end())
        return &it->second;

    // Fall back to field scopes
    return lookupInFieldScopes(key);
}

/// @brief Look up a symbol (const), checking the main table then field scopes.
/// @return Pointer to the symbol info, or nullptr if undefined.
const SymbolInfo *SymbolTable::lookup(std::string_view name) const {
    // Heterogeneous lookup, no allocation
    const std::string key = canonicalSymbolKey(name);
    if (auto it = symbols_.find(key); it != symbols_.end())
        return &it->second;

    return lookupInFieldScopes(key);
}

/// @brief Test whether a symbol (in the main table or a field scope) exists.
bool SymbolTable::contains(std::string_view name) const {
    return lookup(name) != nullptr;
}

/// @brief Remove a symbol from the main table.
/// @return True if a symbol was erased; false if it was not present.
bool SymbolTable::remove(std::string_view name) {
    // Heterogeneous lookup for erase
    const std::string key = canonicalSymbolKey(name);
    auto it = symbols_.find(key);
    if (it != symbols_.end()) {
        symbols_.erase(it);
        return true;
    }
    return false;
}

/// @brief Reset per-procedure symbol state while preserving interned string literals.
/// @details Symbols carrying a string label are kept (with their mutable state reset) for
///          cross-procedure literal deduplication; all others are erased. Field scopes are
///          cleared.
void SymbolTable::resetForNewProcedure() {
    // Preserve symbols with string labels (for literal deduplication)
    for (auto it = symbols_.begin(); it != symbols_.end();) {
        SymbolInfo &info = it->second;
        if (!info.stringLabel.empty()) {
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
        } else {
            it = symbols_.erase(it);
        }
    }

    // Clear field scopes
    fieldScopes_.clear();
}

/// @brief Remove all symbols and field scopes.
void SymbolTable::clear() {
    symbols_.clear();
    fieldScopes_.clear();
}

// =============================================================================
// Type Operations
// =============================================================================

/// @brief Set a symbol's explicit type (marking it boolean when applicable).
void SymbolTable::setType(std::string_view name, AstType type) {
    auto &info = define(name);
    info.type = type;
    info.hasType = true;
    info.isBoolean = !info.isArray && type == AstType::Bool;
}

/// @brief Get a symbol's recorded type.
/// @return The type, or nullopt if the symbol is undefined.
std::optional<SymbolTable::AstType> SymbolTable::getType(std::string_view name) const {
    const auto *info = lookup(name);
    if (!info)
        return std::nullopt;
    return info->type;
}

/// @brief Test whether a symbol has an explicitly assigned (vs inferred) type.
bool SymbolTable::hasExplicitType(std::string_view name) const {
    const auto *info = lookup(name);
    return info && info->hasType;
}

// =============================================================================
// Symbol Classification
// =============================================================================

/// @brief Mark a symbol as referenced, inferring its type if not already explicit.
/// @param name Symbol name.
/// @param inferredType Optional type to apply; falls back to BASIC suffix-based inference.
void SymbolTable::markReferenced(std::string_view name, std::optional<AstType> inferredType) {
    if (name.empty())
        return;

    auto &info = define(name);

    // Apply inferred type if symbol doesn't have an explicit type
    if (!info.hasType) {
        if (inferredType) {
            info.type = *inferredType;
        } else {
            // Fall back to suffix-based inference
            info.type = inferAstTypeFromName(name);
        }
        info.hasType = true;
        info.isBoolean = !info.isArray && info.type == AstType::Bool;
    }

    info.referenced = true;
}

/// @brief Mark a symbol as an array (pointer-typed; clears the boolean flag).
void SymbolTable::markArray(std::string_view name) {
    if (name.empty())
        return;

    auto &info = define(name);
    info.isArray = true;
    // Arrays are pointer-typed; clear boolean flag
    if (info.isBoolean)
        info.isBoolean = false;
}

/// @brief Mark a symbol as a STATIC local (persists across procedure calls).
void SymbolTable::markStatic(std::string_view name) {
    if (name.empty())
        return;

    auto &info = define(name);
    info.isStatic = true;
}

/// @brief Mark a symbol as an object reference of the given class.
/// @param name Symbol name.
/// @param className Owning class name (recorded as the symbol's object class).
void SymbolTable::markObject(std::string_view name, std::string className) {
    if (name.empty())
        return;

    auto &info = define(name);
    info.isObject = true;
    info.objectClass = std::move(className);
    info.hasType = true;
}

/// @brief Mark a symbol as a by-reference parameter (borrowed; not released by the callee).
void SymbolTable::markByRef(std::string_view name) {
    if (name.empty())
        return;

    auto &info = define(name);
    info.isByRefParam = true;
}

// =============================================================================
// Symbol Query
// =============================================================================

/// @brief True if the named symbol is an array.
bool SymbolTable::isArray(std::string_view name) const {
    const auto *info = lookup(name);
    return info && info->isArray;
}

/// @brief True if the named symbol is an object reference.
bool SymbolTable::isObject(std::string_view name) const {
    const auto *info = lookup(name);
    return info && info->isObject;
}

/// @brief True if the named symbol is a STATIC local.
bool SymbolTable::isStatic(std::string_view name) const {
    const auto *info = lookup(name);
    return info && info->isStatic;
}

/// @brief True if the named symbol is a by-reference parameter.
bool SymbolTable::isByRef(std::string_view name) const {
    const auto *info = lookup(name);
    return info && info->isByRefParam;
}

/// @brief True if the named symbol has been referenced.
bool SymbolTable::isReferenced(std::string_view name) const {
    const auto *info = lookup(name);
    return info && info->referenced;
}

/// @brief Get the object-class name of a symbol, or "" if it is not an object.
std::string SymbolTable::getObjectClass(std::string_view name) const {
    const auto *info = lookup(name);
    if (!info || !info->isObject)
        return {};
    return info->objectClass;
}

// =============================================================================
// Slot Management
// =============================================================================

/// @brief Associate a symbol with its IL storage-slot id.
void SymbolTable::setSlotId(std::string_view name, unsigned slotId) {
    auto &info = define(name);
    info.slotId = slotId;
}

/// @brief Get a symbol's storage-slot id, or nullopt if unset/undefined.
std::optional<unsigned> SymbolTable::getSlotId(std::string_view name) const {
    const auto *info = lookup(name);
    if (!info)
        return std::nullopt;
    return info->slotId;
}

/// @brief Associate an array symbol with the slot holding its length.
void SymbolTable::setArrayLengthSlot(std::string_view name, unsigned slotId) {
    auto &info = define(name);
    info.arrayLengthSlot = slotId;
}

/// @brief Get an array symbol's length-slot id, or nullopt if unset/undefined.
std::optional<unsigned> SymbolTable::getArrayLengthSlot(std::string_view name) const {
    const auto *info = lookup(name);
    if (!info)
        return std::nullopt;
    return info->arrayLengthSlot;
}

// =============================================================================
// String Literal Caching
// =============================================================================

/// @brief Record the IL global label of an interned string literal for a symbol.
void SymbolTable::setStringLabel(std::string_view name, std::string label) {
    auto &info = define(name);
    info.stringLabel = std::move(label);
}

/// @brief Get a symbol's interned string-literal label, or "" if none.
std::string SymbolTable::getStringLabel(std::string_view name) const {
    const auto *info = lookup(name);
    if (!info)
        return {};
    return info->stringLabel;
}

/// @brief Test whether a symbol carries an interned string-literal label.
bool SymbolTable::hasStringLabel(std::string_view name) const {
    const auto *info = lookup(name);
    return info && !info->stringLabel.empty();
}

// =============================================================================
// Field Scope Management
// =============================================================================

/// @brief Push a field scope for the given class layout (active during method lowering).
/// @param layout Class layout whose fields become implicitly resolvable (null pushes an empty
///        scope).
/// @details Pre-populates the scope with a symbol per layout field so unqualified field names
///          resolve to the implicit `ME` receiver.
void SymbolTable::pushFieldScope(const ClassLayout *layout) {
    FieldScope scope;
    scope.layout = layout;

    if (layout) {
        // Populate field symbols from the class layout
        for (const auto &field : layout->fields) {
            SymbolInfo info;
            info.type = field.type;
            info.hasType = true;
            info.isArray = field.isArray;
            info.isBoolean = (field.type == AstType::Bool);
            info.referenced = false;
            info.isObject = !field.objectClassName.empty();
            info.objectClass = field.objectClassName;
            scope.symbols.emplace(canonicalSymbolKey(field.name), std::move(info));
        }
    }

    fieldScopes_.push_back(std::move(scope));
}

/// @brief Pop the innermost field scope (no-op if none is active).
void SymbolTable::popFieldScope() {
    if (!fieldScopes_.empty())
        fieldScopes_.pop_back();
}

/// @brief Test whether @p name resolves to a field in any active field scope.
bool SymbolTable::isFieldInScope(std::string_view name) const {
    if (name.empty())
        return false;

    // Heterogeneous lookup, no allocation
    for (auto it = fieldScopes_.rbegin(); it != fieldScopes_.rend(); ++it) {
        if (it->symbols.find(canonicalSymbolKey(name)) != it->symbols.end())
            return true;
    }
    return false;
}

/// @brief Return the innermost active field scope, or nullptr if none.
const FieldScope *SymbolTable::activeFieldScope() const {
    if (fieldScopes_.empty())
        return nullptr;
    return &fieldScopes_.back();
}

// =============================================================================
// Internal Helpers
// =============================================================================

/// @brief Search active field scopes (innermost first) for a field symbol (mutable).
/// @return Pointer to the field's symbol info, or nullptr if not found.
SymbolInfo *SymbolTable::lookupInFieldScopes(std::string_view name) {
    // Heterogeneous lookup, no allocation
    for (auto scopeIt = fieldScopes_.rbegin(); scopeIt != fieldScopes_.rend(); ++scopeIt) {
        auto symIt = scopeIt->symbols.find(name);
        if (symIt != scopeIt->symbols.end())
            return &symIt->second;
    }
    return nullptr;
}

/// @brief Search active field scopes (innermost first) for a field symbol (const).
/// @return Pointer to the field's symbol info, or nullptr if not found.
const SymbolInfo *SymbolTable::lookupInFieldScopes(std::string_view name) const {
    // Heterogeneous lookup, no allocation
    for (auto scopeIt = fieldScopes_.rbegin(); scopeIt != fieldScopes_.rend(); ++scopeIt) {
        auto symIt = scopeIt->symbols.find(name);
        if (symIt != scopeIt->symbols.end())
            return &symIt->second;
    }
    return nullptr;
}

} // namespace il::frontends::basic
