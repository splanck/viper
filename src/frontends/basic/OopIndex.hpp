//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/OopIndex.hpp
// Purpose: Pure data model for OOP metadata without AST dependencies.
// Key invariants: Index stores one entry per class name with immutable signature data.
// Ownership/Lifetime: OopIndex stores copies of metadata without owning AST nodes.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/BasicTypes.hpp"
#include "frontends/basic/ast/NodeFwd.hpp"
#include "support/source_location.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace il::frontends::basic
{

/// @brief Hash functor for heterogeneous string lookup (C++20).
struct OopStringHash
{
    using is_transparent = void;

    template <typename T>
    [[nodiscard]] std::size_t operator()(const T &key) const noexcept
    {
        return std::hash<std::string_view>{}(std::string_view(key));
    }
};

/// @brief Signature used for interface slots (parameters + return type).
struct IfaceMethodSig
{
    std::string name;               ///< Method name within the interface.
    std::vector<Type> paramTypes;   ///< Parameter types in order.
    std::optional<Type> returnType; ///< Optional return type.
};

/// @brief Interface metadata including stable ID and slot layout.
struct InterfaceInfo
{
    int ifaceId = -1;                  ///< Monotonic stable interface identifier.
    std::string qualifiedName;         ///< Fully-qualified interface name (A.B.I).
    std::vector<IfaceMethodSig> slots; ///< Declared methods in slot order.
};

/// @brief Captures the signature of a CLASS method.
struct MethodSig
{
    /// Ordered parameter types, excluding the implicit instance parameter.
    std::vector<Type> paramTypes;

    /// Optional return type for methods producing a value.
    std::optional<Type> returnType;

    /// BUG-099 fix: Qualified class name when method returns an object.
    /// Empty string indicates primitive or void return type.
    std::string returnClassName;

    /// Access specifier for the method (default Public).
    Access access{Access::Public};
};

/// @brief Aggregated information extracted from a CLASS declaration.
struct ClassInfo
{
    /// @brief Field metadata copied from the CLASS definition.
    struct FieldInfo
    {
        std::string name;                    ///< Declared field name.
        Type type = Type::I64;               ///< Declared field type.
        Access access{Access::Public};       ///< Field access control.
        bool isArray{false};                 ///< Whether field is an array (BUG-059 fix).
        std::vector<long long> arrayExtents; ///< Array dimensions if isArray (BUG-059 fix).
        std::string objectClassName;         ///< BUG-082 fix: Class name for object fields.
    };

    /// @brief Signature metadata for constructor parameters.
    struct CtorParam
    {
        Type type = Type::I64; ///< Declared parameter type.
        bool isArray = false;  ///< True when parameter declared with trailing ().
    };

    std::string name;              ///< Unqualified class identifier.
    std::string qualifiedName;     ///< Fully-qualified class name (namespaces + name).
    std::string baseQualified;     ///< Fully-qualified base name (empty when none or unresolved).
    bool isAbstract{false};        ///< True when class is abstract.
    bool isFinal{false};           ///< True when class is final.
    il::support::SourceLoc loc{};  ///< Location of the CLASS keyword.
    std::vector<FieldInfo> fields; ///< Ordered instance field declarations.
    std::vector<FieldInfo> staticFields; ///< Ordered static field declarations.
    bool hasConstructor = false;         ///< True if CLASS declares a constructor.
    bool hasSynthCtor = false;           ///< True when lowering must synthesise a constructor.
    bool hasDestructor = false;          ///< True if CLASS declares a destructor.
    bool hasStaticCtor = false;          ///< True if CLASS declares a static constructor.
    std::vector<CtorParam> ctorParams;   ///< Constructor signature if declared.

    /// @brief Extended method metadata used for vtable construction and checks.
    struct MethodInfo
    {
        MethodSig sig;                   ///< Signature (params/return/access).
        bool isStatic = false;           ///< True when declared STATIC (no implicit receiver).
        bool isVirtual = false;          ///< Declared or implied virtual.
        bool isAbstract = false;         ///< Declared abstract.
        bool isFinal = false;            ///< Declared final.
        int slot = -1;                   ///< Virtual slot index; -1 for non-virtual.
        bool isPropertyAccessor = false; ///< True when synthesized from a PROPERTY.
        bool isGetter = false;           ///< True for getter; false for setter when accessor.
    };

    /// Declared methods indexed by name (heterogeneous lookup enabled).
    std::unordered_map<std::string, MethodInfo, OopStringHash, std::equal_to<>> methods;

    /// Ordered virtual method names by slot for deterministic ABI layout.
    std::vector<std::string> vtable;

    /// Method declaration source locations (for diagnostics).
    std::unordered_map<std::string, il::support::SourceLoc, OopStringHash, std::equal_to<>> methodLocs;

    /// Interfaces implemented by this class (by stable ID).
    std::vector<int> implementedInterfaces;

    /// Mapping from interface id to concrete method mappings (slot -> method name).
    std::unordered_map<int, std::vector<std::string>> ifaceSlotImpl;

    /// Raw implements list captured during parsing (dotted names, unresolved).
    std::vector<std::string> rawImplements;
};

/// @brief Container mapping class names to extracted metadata.
class OopIndex
{
  public:
    using ClassTable = std::unordered_map<std::string, ClassInfo>;
    using IfaceTable = std::unordered_map<std::string, InterfaceInfo>; // key: qualified name

    /// @brief Access the mutable class table.
    [[nodiscard]] ClassTable &classes() noexcept
    {
        return classes_;
    }

    /// @brief Access the immutable class table.
    [[nodiscard]] const ClassTable &classes() const noexcept
    {
        return classes_;
    }

    /// @brief Remove all indexed classes.
    void clear() noexcept
    {
        classes_.clear();
        interfacesByQname_.clear();
        nextInterfaceId_ = 0;
    }

    /// @brief Find a class by name.
    [[nodiscard]] ClassInfo *findClass(const std::string &name);

    /// @brief Find a class by name (const overload).
    [[nodiscard]] const ClassInfo *findClass(const std::string &name) const;

    // =========================================================================
    // Field Query API
    // =========================================================================

    /// @brief Find a field in a class (case-insensitive).
    /// @param className Qualified class name.
    /// @param fieldName Field identifier to find.
    /// @return Pointer to field info or nullptr if not found.
    [[nodiscard]] const ClassInfo::FieldInfo *findField(const std::string &className,
                                                        std::string_view fieldName) const;

    /// @brief Find a field in a class or any of its base classes (case-insensitive).
    /// @param className Qualified class name to start search from.
    /// @param fieldName Field identifier to find.
    /// @return Pointer to field info or nullptr if not found in hierarchy.
    [[nodiscard]] const ClassInfo::FieldInfo *findFieldInHierarchy(
        const std::string &className, std::string_view fieldName) const;

    // =========================================================================
    // Method Query API
    // =========================================================================

    /// @brief Find a method in a class by name.
    /// @param className Qualified class name.
    /// @param methodName Method identifier to find.
    /// @return Pointer to method info or nullptr if not found.
    [[nodiscard]] const ClassInfo::MethodInfo *findMethod(const std::string &className,
                                                          std::string_view methodName) const;

    /// @brief Find a method in a class or any of its base classes.
    /// @param className Qualified class name to start search from.
    /// @param methodName Method identifier to find.
    /// @return Pointer to method info or nullptr if not found in hierarchy.
    [[nodiscard]] const ClassInfo::MethodInfo *findMethodInHierarchy(
        const std::string &className, std::string_view methodName) const;

    /// @brief Access the interface table by qualified name.
    [[nodiscard]] IfaceTable &interfacesByQname() noexcept
    {
        return interfacesByQname_;
    }

    [[nodiscard]] const IfaceTable &interfacesByQname() const noexcept
    {
        return interfacesByQname_;
    }

    /// @brief Allocate the next stable interface ID.
    int allocateInterfaceId() noexcept
    {
        return nextInterfaceId_++;
    }

  private:
    ClassTable classes_;
    IfaceTable interfacesByQname_;
    int nextInterfaceId_ = 0;
};

/// @brief Query the virtual slot for a method if it is virtual.
/// @param index OOP index containing class metadata.
/// @param qualifiedClass Fully-qualified class name.
/// @param methodName Method identifier.
/// @return Slot index (>=0) when virtual; -1 for non-virtual or when not found.
int getVirtualSlot(const OopIndex &index,
                   const std::string &qualifiedClass,
                   const std::string &methodName);

} // namespace il::frontends::basic