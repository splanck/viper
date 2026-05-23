//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/zia/LowererSymbolTable.hpp
// Purpose: Symbol table management for the Zia IL lowerer.
//
// Key invariants:
//   - Local names are unique within a function scope
//   - Global constants are populated before any function body is lowered
//
// Ownership/Lifetime:
//   - Owned by Lowerer
//   - Locals/slots are cleared between functions
//   - Globals persist for the entire module lowering
//
// Links: frontends/zia/Lowerer.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/zia/Types.hpp"
#include "il/core/Value.hpp"
#include <string>
#include <unordered_map>

namespace il::frontends::zia {

/// @brief Name-keyed symbol storage for the Zia lowerer.
/// @details Holds function-scoped locals (SSA values), their semantic types, slot pointers
///          (for mutable/cross-block variables), and module-scoped global constants, variables,
///          and initializers. Locals/types/slots are cleared between functions via
///          clearFunctionScope(); globals persist for the whole module.
/// @invariant Local names are unique within a function scope.
/// @invariant Global constants are populated before any function body is lowered.
class LowererSymbolTable {
  public:
    using Value = il::core::Value;

    LowererSymbolTable() = default;

    /// @brief Bind a local name to an SSA value.
    void defineLocal(const std::string &name, Value value) {
        locals_[name] = value;
    }

    /// @brief Look up a local by name.
    /// @return Pointer to the value, or nullptr if not defined.
    Value *lookupLocal(const std::string &name) {
        auto it = locals_.find(name);
        return it != locals_.end() ? &it->second : nullptr;
    }

    /// @brief Read-only access to all locals in scope.
    const std::unordered_map<std::string, Value> &locals() const {
        return locals_;
    }

    /// @brief Mutable access to all locals in scope.
    std::unordered_map<std::string, Value> &locals() {
        return locals_;
    }

    /// @brief Drop all locals (used when leaving a function).
    void clearLocals() {
        locals_.clear();
    }

    /// @brief Record the semantic type of a local.
    void setLocalType(const std::string &name, TypeRef type) {
        localTypes_[name] = type;
    }

    /// @brief Look up a local's semantic type.
    /// @return The type, or nullptr if unknown.
    TypeRef lookupLocalType(const std::string &name) const {
        auto it = localTypes_.find(name);
        return it != localTypes_.end() ? it->second : nullptr;
    }

    /// @brief Read-only access to all local types.
    const std::unordered_map<std::string, TypeRef> &localTypes() const {
        return localTypes_;
    }

    /// @brief Mutable access to all local types.
    std::unordered_map<std::string, TypeRef> &localTypes() {
        return localTypes_;
    }

    /// @brief Drop all local type records.
    void clearLocalTypes() {
        localTypes_.clear();
    }

    /// @brief Associate a name with its stack-slot pointer (mutable/cross-block variable).
    void registerSlot(const std::string &name, Value slot) {
        slots_[name] = slot;
    }

    /// @brief Look up a variable's slot pointer.
    /// @return Pointer to the slot value, or nullptr if the variable is not slot-backed.
    Value *lookupSlot(const std::string &name) {
        auto it = slots_.find(name);
        return it != slots_.end() ? &it->second : nullptr;
    }

    /// @brief Forget a variable's slot (e.g. when a scratch loop variable goes out of scope).
    void removeSlot(const std::string &name) {
        slots_.erase(name);
    }

    /// @brief Read-only access to all slot bindings.
    const std::unordered_map<std::string, Value> &slots() const {
        return slots_;
    }

    /// @brief Mutable access to all slot bindings.
    std::unordered_map<std::string, Value> &slots() {
        return slots_;
    }

    /// @brief Drop all slot bindings.
    void clearSlots() {
        slots_.clear();
    }

    /// @brief Record a module-level compile-time constant by (qualified) name.
    void defineGlobalConstant(const std::string &name, Value value) {
        globalConstants_[name] = value;
    }

    /// @brief Look up a global constant.
    /// @return Pointer to the constant value, or nullptr if none exists.
    const Value *lookupGlobalConstant(const std::string &name) const {
        auto it = globalConstants_.find(name);
        return it != globalConstants_.end() ? &it->second : nullptr;
    }

    /// @brief Test whether a global constant with this name is defined.
    bool hasGlobalConstant(const std::string &name) const {
        return globalConstants_.count(name) > 0;
    }

    /// @brief Mutable access to all global constants.
    std::unordered_map<std::string, Value> &globalConstants() {
        return globalConstants_;
    }

    /// @brief Read-only access to all global constants.
    const std::unordered_map<std::string, Value> &globalConstants() const {
        return globalConstants_;
    }

    /// @brief Record a module-level mutable variable and its type.
    void defineGlobalVariable(const std::string &name, TypeRef type) {
        globalVariables_[name] = type;
    }

    /// @brief Look up a global variable's type.
    /// @return The type, or nullptr if no such global exists.
    TypeRef lookupGlobalVariable(const std::string &name) const {
        auto it = globalVariables_.find(name);
        return it != globalVariables_.end() ? it->second : nullptr;
    }

    /// @brief Mutable access to all global variables.
    std::unordered_map<std::string, TypeRef> &globalVariables() {
        return globalVariables_;
    }

    /// @brief Read-only access to all global variables.
    const std::unordered_map<std::string, TypeRef> &globalVariables() const {
        return globalVariables_;
    }

    /// @brief Record the lowered initializer value for a global.
    void defineGlobalInitializer(const std::string &name, Value value) {
        globalInitializers_[name] = value;
    }

    /// @brief Look up a global's initializer value.
    /// @return Pointer to the initializer value, or nullptr if none was recorded.
    const Value *lookupGlobalInitializer(const std::string &name) const {
        auto it = globalInitializers_.find(name);
        return it != globalInitializers_.end() ? &it->second : nullptr;
    }

    /// @brief Mutable access to all global initializers.
    std::unordered_map<std::string, Value> &globalInitializers() {
        return globalInitializers_;
    }

    /// @brief Read-only access to all global initializers.
    const std::unordered_map<std::string, Value> &globalInitializers() const {
        return globalInitializers_;
    }

    /// @brief Clear all function-scoped state (locals, local types, slots) between functions.
    void clearFunctionScope() {
        locals_.clear();
        localTypes_.clear();
        slots_.clear();
    }

  private:
    std::unordered_map<std::string, Value> locals_;
    std::unordered_map<std::string, TypeRef> localTypes_;
    std::unordered_map<std::string, Value> slots_;
    std::unordered_map<std::string, Value> globalConstants_;
    std::unordered_map<std::string, TypeRef> globalVariables_;
    std::unordered_map<std::string, Value> globalInitializers_;
};

} // namespace il::frontends::zia
