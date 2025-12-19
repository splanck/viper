//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/viperlang/Types.hpp
// Purpose: Semantic type representation for ViperLang.
// Key invariants: Types are immutable after construction.
// Ownership/Lifetime: Types are shared via shared_ptr for interning.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Type.hpp"
#include <memory>
#include <string>
#include <vector>

namespace il::frontends::viperlang
{

// Forward declaration
struct ViperType;
using TypeRef = std::shared_ptr<const ViperType>;

/// @brief Semantic type kinds for ViperLang.
enum class TypeKindSem
{
    // Primitive types
    Integer, // i64 - 64-bit signed integer
    Number,  // f64 - 64-bit floating point
    Boolean, // i1 (stored as i64) - true/false
    String,  // str - UTF-8 string reference
    Byte,    // i32 (NOT i8 - IL doesn't have i8)
    Unit,    // void - single value () for Result[Unit]
    Void,    // No return type for functions

    // Wrapper types
    Optional, // T? - nullable type
    Result,   // Result[T] - Ok/Err sum type

    // Collection types
    List, // List[T] - dynamic array
    Map,  // Map[K,V] - key-value collection
    Set,  // Set[T] - unique elements

    // Function type
    Function, // (A, B) -> C

    // User-defined types
    Value,     // value type (copy semantics)
    Entity,    // entity type (reference semantics)
    Interface, // interface type

    // Special types
    Error,   // Error value type
    Ptr,     // Opaque pointer (for thread args, etc.)
    Unknown, // Placeholder for type inference
    Never,   // Bottom type (never returns)
    Any,     // Top type (for interop)

    // Generic type parameter
    TypeParam, // T, U, etc.
};

/// @brief Semantic type representation.
/// @details Represents resolved types after parsing and name resolution.
struct ViperType
{
    TypeKindSem kind;
    std::string name;              // For Value, Entity, Interface, TypeParam
    std::vector<TypeRef> typeArgs; // For generics: List[T], Map[K,V], etc.

    /// @brief Default constructor creates Unknown type.
    ViperType() : kind(TypeKindSem::Unknown) {}

    /// @brief Construct primitive type.
    explicit ViperType(TypeKindSem k) : kind(k) {}

    /// @brief Construct named type (Value, Entity, Interface, TypeParam).
    ViperType(TypeKindSem k, std::string n) : kind(k), name(std::move(n)) {}

    /// @brief Construct generic type.
    ViperType(TypeKindSem k, std::vector<TypeRef> args) : kind(k), typeArgs(std::move(args)) {}

    /// @brief Construct named generic type (e.g., custom generic value).
    ViperType(TypeKindSem k, std::string n, std::vector<TypeRef> args)
        : kind(k), name(std::move(n)), typeArgs(std::move(args))
    {
    }

    //=========================================================================
    // Type Predicates
    //=========================================================================

    bool isPrimitive() const
    {
        switch (kind)
        {
            case TypeKindSem::Integer:
            case TypeKindSem::Number:
            case TypeKindSem::Boolean:
            case TypeKindSem::String:
            case TypeKindSem::Byte:
            case TypeKindSem::Unit:
                return true;
            default:
                return false;
        }
    }

    bool isNumeric() const
    {
        return kind == TypeKindSem::Integer || kind == TypeKindSem::Number ||
               kind == TypeKindSem::Byte;
    }

    bool isIntegral() const
    {
        return kind == TypeKindSem::Integer || kind == TypeKindSem::Byte;
    }

    bool isReference() const
    {
        return kind == TypeKindSem::Entity || kind == TypeKindSem::Interface ||
               kind == TypeKindSem::List || kind == TypeKindSem::Map || kind == TypeKindSem::Set;
    }

    bool isOptional() const
    {
        return kind == TypeKindSem::Optional;
    }

    bool isResult() const
    {
        return kind == TypeKindSem::Result;
    }

    bool isVoid() const
    {
        return kind == TypeKindSem::Void;
    }

    bool isUnit() const
    {
        return kind == TypeKindSem::Unit;
    }

    bool isUnknown() const
    {
        return kind == TypeKindSem::Unknown;
    }

    bool isNever() const
    {
        return kind == TypeKindSem::Never;
    }

    bool isCallable() const
    {
        return kind == TypeKindSem::Function;
    }

    bool isGeneric() const
    {
        return !typeArgs.empty();
    }

    /// @brief Check if this is a user-defined type.
    bool isUserDefined() const
    {
        return kind == TypeKindSem::Value || kind == TypeKindSem::Entity ||
               kind == TypeKindSem::Interface;
    }

    /// @brief Get inner type for Optional[T].
    TypeRef innerType() const
    {
        if (kind == TypeKindSem::Optional && !typeArgs.empty())
            return typeArgs[0];
        return nullptr;
    }

    /// @brief Get success type for Result[T].
    TypeRef successType() const
    {
        if (kind == TypeKindSem::Result && !typeArgs.empty())
            return typeArgs[0];
        return nullptr;
    }

    /// @brief Get element type for List[T] or Set[T].
    TypeRef elementType() const
    {
        if ((kind == TypeKindSem::List || kind == TypeKindSem::Set) && !typeArgs.empty())
            return typeArgs[0];
        return nullptr;
    }

    /// @brief Get key type for Map[K,V].
    TypeRef keyType() const
    {
        if (kind == TypeKindSem::Map && typeArgs.size() >= 2)
            return typeArgs[0];
        return nullptr;
    }

    /// @brief Get value type for Map[K,V].
    TypeRef valueType() const
    {
        if (kind == TypeKindSem::Map && typeArgs.size() >= 2)
            return typeArgs[1];
        return nullptr;
    }

    /// @brief Get return type for Function types.
    TypeRef returnType() const
    {
        if (kind == TypeKindSem::Function && !typeArgs.empty())
            return typeArgs.back();
        return nullptr;
    }

    /// @brief Get parameter types for Function types.
    std::vector<TypeRef> paramTypes() const
    {
        if (kind == TypeKindSem::Function && typeArgs.size() > 1)
        {
            return std::vector<TypeRef>(typeArgs.begin(), typeArgs.end() - 1);
        }
        return {};
    }

    //=========================================================================
    // Type Comparison
    //=========================================================================

    bool equals(const ViperType &other) const;
    bool isAssignableFrom(const ViperType &source) const;
    bool isConvertibleTo(const ViperType &target) const;

    //=========================================================================
    // String Representation
    //=========================================================================

    std::string toString() const;
};

//=============================================================================
// Type Factory Functions
//=============================================================================

/// @brief Singleton type instances for primitives.
namespace types
{

// Primitive types
TypeRef integer();
TypeRef number();
TypeRef boolean();
TypeRef string();
TypeRef byte();
TypeRef unit();
TypeRef voidType();
TypeRef error();
TypeRef ptr();
TypeRef unknown();
TypeRef never();
TypeRef any();

// Generic type constructors
TypeRef optional(TypeRef inner);
TypeRef result(TypeRef successType);
TypeRef list(TypeRef element);
TypeRef set(TypeRef element);
TypeRef map(TypeRef key, TypeRef value);
TypeRef function(std::vector<TypeRef> params, TypeRef ret);

// User-defined type constructors
TypeRef value(const std::string &name, std::vector<TypeRef> typeParams = {});
TypeRef entity(const std::string &name, std::vector<TypeRef> typeParams = {});
TypeRef interface(const std::string &name, std::vector<TypeRef> typeParams = {});
TypeRef typeParam(const std::string &name);

} // namespace types

//=============================================================================
// IL Type Mapping
//=============================================================================

/// @brief Maps ViperLang semantic types to IL primitive types.
/// @param type The ViperLang type to map.
/// @return The corresponding IL type kind.
/// @note Reference types (Entity, collections) map to Ptr.
/// @note Optionals of value types require special handling (flag + value).
il::core::Type::Kind toILType(const ViperType &type);

/// @brief Get the size in bytes for a type in memory.
/// @param type The type to get the size for.
/// @return Size in bytes, or 0 for unsized types.
size_t typeSize(const ViperType &type);

/// @brief Get the alignment in bytes for a type.
/// @param type The type to get alignment for.
/// @return Alignment in bytes.
size_t typeAlignment(const ViperType &type);

/// @brief Convert type kind to human-readable string.
const char *kindToString(TypeKindSem kind);

} // namespace il::frontends::viperlang
