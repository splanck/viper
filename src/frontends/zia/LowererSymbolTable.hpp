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

class LowererSymbolTable {
  public:
    using Value = il::core::Value;

    LowererSymbolTable() = default;

    void defineLocal(const std::string &name, Value value) {
        locals_[name] = value;
    }

    Value *lookupLocal(const std::string &name) {
        auto it = locals_.find(name);
        return it != locals_.end() ? &it->second : nullptr;
    }

    const std::unordered_map<std::string, Value> &locals() const {
        return locals_;
    }

    std::unordered_map<std::string, Value> &locals() {
        return locals_;
    }

    void clearLocals() {
        locals_.clear();
    }

    void setLocalType(const std::string &name, TypeRef type) {
        localTypes_[name] = type;
    }

    TypeRef lookupLocalType(const std::string &name) const {
        auto it = localTypes_.find(name);
        return it != localTypes_.end() ? it->second : nullptr;
    }

    const std::unordered_map<std::string, TypeRef> &localTypes() const {
        return localTypes_;
    }

    std::unordered_map<std::string, TypeRef> &localTypes() {
        return localTypes_;
    }

    void clearLocalTypes() {
        localTypes_.clear();
    }

    void registerSlot(const std::string &name, Value slot) {
        slots_[name] = slot;
    }

    Value *lookupSlot(const std::string &name) {
        auto it = slots_.find(name);
        return it != slots_.end() ? &it->second : nullptr;
    }

    void removeSlot(const std::string &name) {
        slots_.erase(name);
    }

    const std::unordered_map<std::string, Value> &slots() const {
        return slots_;
    }

    std::unordered_map<std::string, Value> &slots() {
        return slots_;
    }

    void clearSlots() {
        slots_.clear();
    }

    void defineGlobalConstant(const std::string &name, Value value) {
        globalConstants_[name] = value;
    }

    const Value *lookupGlobalConstant(const std::string &name) const {
        auto it = globalConstants_.find(name);
        return it != globalConstants_.end() ? &it->second : nullptr;
    }

    bool hasGlobalConstant(const std::string &name) const {
        return globalConstants_.count(name) > 0;
    }

    std::unordered_map<std::string, Value> &globalConstants() {
        return globalConstants_;
    }

    const std::unordered_map<std::string, Value> &globalConstants() const {
        return globalConstants_;
    }

    void defineGlobalVariable(const std::string &name, TypeRef type) {
        globalVariables_[name] = type;
    }

    TypeRef lookupGlobalVariable(const std::string &name) const {
        auto it = globalVariables_.find(name);
        return it != globalVariables_.end() ? it->second : nullptr;
    }

    std::unordered_map<std::string, TypeRef> &globalVariables() {
        return globalVariables_;
    }

    const std::unordered_map<std::string, TypeRef> &globalVariables() const {
        return globalVariables_;
    }

    void defineGlobalInitializer(const std::string &name, Value value) {
        globalInitializers_[name] = value;
    }

    const Value *lookupGlobalInitializer(const std::string &name) const {
        auto it = globalInitializers_.find(name);
        return it != globalInitializers_.end() ? &it->second : nullptr;
    }

    std::unordered_map<std::string, Value> &globalInitializers() {
        return globalInitializers_;
    }

    const std::unordered_map<std::string, Value> &globalInitializers() const {
        return globalInitializers_;
    }

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
