//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/SymbolTable.hpp
// Purpose: Unified symbol table abstraction for BASIC frontend.
//
// This module consolidates symbol tracking operations previously scattered
// across Lowerer, SemanticAnalyzer, and related components. It provides a
// clean interface for:
//   - Symbol definition and lookup
//   - Type tracking and inference
//   - Array and object metadata management
//   - Field scope management for OOP constructs
//
// Key Invariants:
//   - Symbol names are stored case-insensitively (canonicalized)
//   - Each symbol has at most one active definition per scope
//   - Field scopes overlay procedure-local symbols during class method lowering
//
// Ownership/Lifetime:
//   - Owned by Lowerer instance
//   - Symbols persist for the duration of a procedure lowering pass
//   - String literal labels persist across procedure boundaries
//
// Links: docs/architecture.md, docs/codemap.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/basic/ast/NodeFwd.hpp"
#include "frontends/basic/LowererTypes.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace il::frontends::basic
{

// Forward declarations
class ClassLayout;

/// @brief Unified symbol table for BASIC variable and type tracking.
///
/// Consolidates symbol operations into a single abstraction with clear
/// semantics for definition, lookup, and type inference.
class SymbolTable
{
  public:
    using AstType = ::il::frontends::basic::Type;

    /// @brief Default constructor creates an empty symbol table.
    SymbolTable() = default;

    // =========================================================================
    // Core Symbol Operations
    // =========================================================================

    /// @brief Ensure a symbol exists, creating with defaults if absent.
    /// @param name Symbol identifier (case-insensitive).
    /// @return Reference to the (possibly new) symbol record.
    SymbolInfo &define(std::string_view name);

    /// @brief Look up a symbol without creating it.
    /// @param name Symbol identifier to find.
    /// @return Pointer to symbol info, or nullptr if not found.
    [[nodiscard]] SymbolInfo *lookup(std::string_view name);

    /// @brief Const lookup for read-only access.
    [[nodiscard]] const SymbolInfo *lookup(std::string_view name) const;

    /// @brief Check if a symbol is defined.
    [[nodiscard]] bool contains(std::string_view name) const;

    /// @brief Remove a symbol from the table.
    /// @return True if the symbol existed and was removed.
    bool remove(std::string_view name);

    /// @brief Clear all symbols except those with cached string labels.
    /// @details Preserves string literal deduplication across procedures.
    void resetForNewProcedure();

    /// @brief Clear all symbols unconditionally.
    void clear();

    // =========================================================================
    // Type Operations
    // =========================================================================

    /// @brief Set the declared type for a symbol.
    /// @param name Symbol identifier.
    /// @param type AST type to assign.
    void setType(std::string_view name, AstType type);

    /// @brief Get the type for a symbol if known.
    [[nodiscard]] std::optional<AstType> getType(std::string_view name) const;

    /// @brief Check if a symbol has an explicitly declared type.
    [[nodiscard]] bool hasExplicitType(std::string_view name) const;

    // =========================================================================
    // Symbol Classification
    // =========================================================================

    /// @brief Mark a symbol as referenced in the current procedure.
    /// @details Also infers type from name suffix if not already set.
    /// @param name Symbol identifier.
    /// @param inferredType Optional type from semantic analysis.
    void markReferenced(std::string_view name, std::optional<AstType> inferredType = std::nullopt);

    /// @brief Mark a symbol as an array.
    void markArray(std::string_view name);

    /// @brief Mark a symbol as a STATIC procedure-local variable.
    void markStatic(std::string_view name);

    /// @brief Mark a symbol as an object reference.
    /// @param name Symbol identifier.
    /// @param className Fully-qualified class name for the object type.
    void markObject(std::string_view name, std::string className);

    /// @brief Mark a symbol as a BYREF parameter.
    void markByRef(std::string_view name);

    // =========================================================================
    // Symbol Query
    // =========================================================================

    /// @brief Check if symbol is an array.
    [[nodiscard]] bool isArray(std::string_view name) const;

    /// @brief Check if symbol is an object reference.
    [[nodiscard]] bool isObject(std::string_view name) const;

    /// @brief Check if symbol is a STATIC variable.
    [[nodiscard]] bool isStatic(std::string_view name) const;

    /// @brief Check if symbol is a BYREF parameter.
    [[nodiscard]] bool isByRef(std::string_view name) const;

    /// @brief Check if symbol has been referenced.
    [[nodiscard]] bool isReferenced(std::string_view name) const;

    /// @brief Get the object class name for an object symbol.
    [[nodiscard]] std::string getObjectClass(std::string_view name) const;

    // =========================================================================
    // Slot Management
    // =========================================================================

    /// @brief Assign a slot ID to a symbol.
    void setSlotId(std::string_view name, unsigned slotId);

    /// @brief Get the slot ID for a symbol.
    [[nodiscard]] std::optional<unsigned> getSlotId(std::string_view name) const;

    /// @brief Set the array length slot for a symbol.
    void setArrayLengthSlot(std::string_view name, unsigned slotId);

    /// @brief Get the array length slot for a symbol.
    [[nodiscard]] std::optional<unsigned> getArrayLengthSlot(std::string_view name) const;

    // =========================================================================
    // String Literal Caching
    // =========================================================================

    /// @brief Cache a string literal label for deduplication.
    void setStringLabel(std::string_view name, std::string label);

    /// @brief Get the cached string label for a symbol.
    [[nodiscard]] std::string getStringLabel(std::string_view name) const;

    /// @brief Check if a symbol has a cached string label.
    [[nodiscard]] bool hasStringLabel(std::string_view name) const;

    // =========================================================================
    // Field Scope Management (OOP)
    // =========================================================================

    /// @brief Push a field scope for class method lowering.
    /// @param layout Class layout providing field definitions.
    void pushFieldScope(const ClassLayout *layout);

    /// @brief Pop the current field scope.
    void popFieldScope();

    /// @brief Check if a name refers to a field in the current scope.
    [[nodiscard]] bool isFieldInScope(std::string_view name) const;

    /// @brief Get the active field scope, if any.
    [[nodiscard]] const FieldScope *activeFieldScope() const;

    // =========================================================================
    // Iteration
    // =========================================================================

    /// @brief Iterate over all symbols (non-const).
    template <typename Func>
    void forEach(Func &&fn)
    {
        for (auto &[name, info] : symbols_)
            fn(name, info);
    }

    /// @brief Iterate over all symbols (const).
    template <typename Func>
    void forEach(Func &&fn) const
    {
        for (const auto &[name, info] : symbols_)
            fn(name, info);
    }

    /// @brief Get the number of symbols in the table.
    [[nodiscard]] std::size_t size() const noexcept
    {
        return symbols_.size();
    }

    /// @brief Check if the table is empty.
    [[nodiscard]] bool empty() const noexcept
    {
        return symbols_.empty();
    }

    // =========================================================================
    // Direct Access (for migration compatibility)
    // =========================================================================

    /// @brief Get direct access to the underlying map.
    /// @note Prefer using define/lookup methods for new code.
    [[nodiscard]] std::unordered_map<std::string, SymbolInfo> &raw() noexcept
    {
        return symbols_;
    }

    [[nodiscard]] const std::unordered_map<std::string, SymbolInfo> &raw() const noexcept
    {
        return symbols_;
    }

  private:
    /// @brief Main symbol storage indexed by canonicalized name.
    std::unordered_map<std::string, SymbolInfo> symbols_;

    /// @brief Stack of field scopes for class method lowering.
    std::vector<FieldScope> fieldScopes_;

    /// @brief Internal helper to find symbol in field scopes.
    [[nodiscard]] SymbolInfo *lookupInFieldScopes(std::string_view name);
    [[nodiscard]] const SymbolInfo *lookupInFieldScopes(std::string_view name) const;
};

} // namespace il::frontends::basic
