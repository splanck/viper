//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/sem/OOPTypes.hpp
// Purpose: OOP-related type structures for Pascal semantic analysis.
// Key invariants: ClassInfo/InterfaceInfo track class metadata for lowering.
// Ownership/Lifetime: Value types, copyable.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/sem/Types.hpp"
#include "support/source_location.hpp"
#include <map>
#include <string>
#include <vector>

namespace il::frontends::pascal
{

//===----------------------------------------------------------------------===//
// Method and Field Information
//===----------------------------------------------------------------------===//

/// @brief Information about a class method.
struct MethodInfo
{
    std::string name;                                    ///< Method name
    std::vector<std::pair<std::string, PasType>> params; ///< Parameter name-type pairs
    std::vector<bool> isVarParam;                        ///< Whether each param is var/out
    std::vector<bool> hasDefault;                        ///< Whether each param has a default value
    PasType returnType;                                  ///< Return type (Void for procedures)
    bool isVirtual{false};                               ///< Marked virtual
    bool isOverride{false};                              ///< Marked override
    bool isAbstract{false};                              ///< Marked abstract
    Visibility visibility{Visibility::Public};           ///< Visibility
    il::support::SourceLoc loc;                          ///< Source location
    size_t requiredParams{0}; ///< Number of required (non-default) params
};

/// @brief Information about a class field.
struct FieldInfo
{
    std::string name;                          ///< Field name
    PasType type;                              ///< Field type
    bool isWeak{false};                        ///< Marked weak
    Visibility visibility{Visibility::Public}; ///< Visibility
    il::support::SourceLoc loc;                ///< Source location
};

/// @brief Information about a property accessor target.
struct PropertyAccessor
{
    enum class Kind
    {
        None,
        Field,
        Method,
    };
    Kind kind{Kind::None};
    std::string name; ///< Field or method name
};

/// @brief Information about a class property.
struct PropertyInfo
{
    std::string name;                          ///< Property name
    PasType type;                              ///< Property type
    PropertyAccessor getter;                   ///< Getter target
    PropertyAccessor setter;                   ///< Setter target (may be None)
    Visibility visibility{Visibility::Public}; ///< Visibility
    il::support::SourceLoc loc;                ///< Source location
};

//===----------------------------------------------------------------------===//
// Class Information
//===----------------------------------------------------------------------===//

/// @brief Information about a class.
struct ClassInfo
{
    std::string name;                    ///< Class name
    std::string baseClass;               ///< Base class name (empty if none)
    std::vector<std::string> interfaces; ///< Implemented interface names
    std::map<std::string, std::vector<MethodInfo>>
        methods;                                    ///< Method name -> overloads (lowercase key)
    std::map<std::string, FieldInfo> fields;        ///< Field name -> info (lowercase key)
    std::map<std::string, PropertyInfo> properties; ///< Property name -> info (lowercase key)
    bool hasConstructor{false};                     ///< Has at least one constructor
    bool hasDestructor{false};                      ///< Has a destructor
    bool isAbstract{false}; ///< True if class declares or inherits abstract methods not implemented
    il::support::SourceLoc loc; ///< Source location

    /// @brief Find a method by name (returns first overload for backwards compatibility).
    /// @param name Method name (lowercase).
    /// @return Pointer to first matching method, or nullptr if not found.
    const MethodInfo *findMethod(const std::string &name) const
    {
        auto it = methods.find(name);
        if (it != methods.end() && !it->second.empty())
            return &it->second.front();
        return nullptr;
    }

    /// @brief Find all overloads of a method by name.
    /// @param name Method name (lowercase).
    /// @return Pointer to vector of overloads, or nullptr if not found.
    const std::vector<MethodInfo> *findOverloads(const std::string &name) const
    {
        auto it = methods.find(name);
        if (it != methods.end())
            return &it->second;
        return nullptr;
    }
};

//===----------------------------------------------------------------------===//
// Interface Information
//===----------------------------------------------------------------------===//

/// @brief Information about an interface.
struct InterfaceInfo
{
    std::string name;                        ///< Interface name
    std::vector<std::string> baseInterfaces; ///< Extended interface names
    std::map<std::string, std::vector<MethodInfo>>
        methods;                ///< Method name -> overloads (lowercase key)
    il::support::SourceLoc loc; ///< Source location

    /// @brief Find a method by name (returns first overload).
    /// @param name Method name (lowercase).
    /// @return Pointer to first matching method, or nullptr if not found.
    const MethodInfo *findMethod(const std::string &name) const
    {
        auto it = methods.find(name);
        if (it != methods.end() && !it->second.empty())
            return &it->second.front();
        return nullptr;
    }

    /// @brief Find all overloads of a method by name.
    /// @param name Method name (lowercase).
    /// @return Pointer to vector of overloads, or nullptr if not found.
    const std::vector<MethodInfo> *findOverloads(const std::string &name) const
    {
        auto it = methods.find(name);
        if (it != methods.end())
            return &it->second;
        return nullptr;
    }
};

//===----------------------------------------------------------------------===//
// Unit Information
//===----------------------------------------------------------------------===//

/// @brief Information about a compiled unit's exports.
struct UnitInfo
{
    std::string name;                                ///< Unit name
    std::map<std::string, PasType> types;            ///< Exported types (lowercase key)
    std::map<std::string, ConstantValue> constants;  ///< Exported constants with values
    std::map<std::string, FuncSignature> functions;  ///< Exported functions/procedures
    std::map<std::string, ClassInfo> classes;        ///< Exported classes
    std::map<std::string, InterfaceInfo> interfaces; ///< Exported interfaces
};

} // namespace il::frontends::pascal
