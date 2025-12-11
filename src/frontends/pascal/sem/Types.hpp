//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/sem/Types.hpp
// Purpose: Core type representation for Pascal semantic analysis.
// Key invariants: PasType captures semantic meaning of types after resolution.
// Ownership/Lifetime: Value types, copyable.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace il::frontends::pascal
{

//===----------------------------------------------------------------------===//
// Type Representation
//===----------------------------------------------------------------------===//

/// @brief Discriminator for Pascal type kinds.
enum class PasTypeKind
{
    Integer,   ///< 64-bit signed integer
    Real,      ///< Double-precision floating-point
    Boolean,   ///< Boolean (True/False)
    String,    ///< String type
    Enum,      ///< Enumeration type
    Array,     ///< Array type (static or dynamic)
    Record,    ///< Record type
    Class,     ///< Class type
    Interface, ///< Interface type
    Optional,  ///< Optional type (T?)
    Pointer,   ///< Pointer type (^T)
    Procedure, ///< Procedure type
    Function,  ///< Function type
    Set,       ///< Set type
    Range,     ///< Subrange type
    Nil,       ///< Nil literal type (assignable to optionals, pointers, classes)
    Unknown,   ///< Unknown/error type
    Void       ///< No value (procedure return)
};

/// @brief Represents a resolved Pascal type.
/// @details This structure captures the semantic meaning of types after
///          resolution from AST TypeNodes. It supports composite types
///          like arrays, optionals, and records.
struct PasType
{
    PasTypeKind kind{PasTypeKind::Unknown};

    /// For Named types: the fully-qualified type name (e.g., "TMyClass")
    std::string name;

    /// For Array: element type
    std::shared_ptr<PasType> elementType;

    /// For Array: dimension count (0 = dynamic array)
    size_t dimensions{0};

    /// For Array: actual sizes of each dimension (e.g., [10] for array[10])
    std::vector<int64_t> arraySizes;

    /// For Optional: wrapped inner type
    std::shared_ptr<PasType> innerType;

    /// For Pointer: pointee type
    std::shared_ptr<PasType> pointeeType;

    /// For Enum: list of enumerator names
    std::vector<std::string> enumValues;

    /// For Enum constants: ordinal value (-1 if not an enum constant)
    int enumOrdinal{-1};

    /// For Record/Class: field name -> type
    std::map<std::string, std::shared_ptr<PasType>> fields;

    /// For Procedure/Function: parameter types
    std::vector<std::shared_ptr<PasType>> paramTypes;

    /// For Function: return type
    std::shared_ptr<PasType> returnType;

    /// @brief Create an unknown type.
    static PasType unknown()
    {
        PasType t;
        t.kind = PasTypeKind::Unknown;
        return t;
    }

    /// @brief Create a void type.
    static PasType voidType()
    {
        PasType t;
        t.kind = PasTypeKind::Void;
        return t;
    }

    /// @brief Create an integer type.
    static PasType integer()
    {
        PasType t;
        t.kind = PasTypeKind::Integer;
        return t;
    }

    /// @brief Create a real type.
    static PasType real()
    {
        PasType t;
        t.kind = PasTypeKind::Real;
        return t;
    }

    /// @brief Create a boolean type.
    static PasType boolean()
    {
        PasType t;
        t.kind = PasTypeKind::Boolean;
        return t;
    }

    /// @brief Create a string type.
    static PasType string()
    {
        PasType t;
        t.kind = PasTypeKind::String;
        return t;
    }

    /// @brief Create a nil type.
    static PasType nil()
    {
        PasType t;
        t.kind = PasTypeKind::Nil;
        return t;
    }

    /// @brief Create an optional type wrapping @p inner.
    static PasType optional(PasType inner)
    {
        PasType t;
        t.kind = PasTypeKind::Optional;
        t.innerType = std::make_shared<PasType>(std::move(inner));
        return t;
    }

    /// @brief Create an array type with @p elem element type.
    static PasType array(PasType elem, size_t dims = 0, std::vector<int64_t> sizes = {})
    {
        PasType t;
        t.kind = PasTypeKind::Array;
        t.elementType = std::make_shared<PasType>(std::move(elem));
        t.dimensions = dims;
        t.arraySizes = std::move(sizes);
        return t;
    }

    /// @brief Create a pointer type to @p pointee.
    static PasType pointer(PasType pointee)
    {
        PasType t;
        t.kind = PasTypeKind::Pointer;
        t.pointeeType = std::make_shared<PasType>(std::move(pointee));
        return t;
    }

    /// @brief Create an enum type with given values.
    static PasType enumType(std::vector<std::string> values)
    {
        PasType t;
        t.kind = PasTypeKind::Enum;
        t.enumValues = std::move(values);
        return t;
    }

    /// @brief Create an enum constant with a specific ordinal.
    /// @param typeName Name of the enum type.
    /// @param values All enum member names (for type identity).
    /// @param ordinal The ordinal value of this constant.
    static PasType enumConstant(std::string typeName, std::vector<std::string> values, int ordinal)
    {
        PasType t;
        t.kind = PasTypeKind::Enum;
        t.name = std::move(typeName);
        t.enumValues = std::move(values);
        t.enumOrdinal = ordinal;
        return t;
    }

    /// @brief Create a class type with a given name.
    static PasType classType(std::string className)
    {
        PasType t;
        t.kind = PasTypeKind::Class;
        t.name = std::move(className);
        return t;
    }

    /// @brief Create an interface type with a given name.
    static PasType interfaceType(std::string interfaceName)
    {
        PasType t;
        t.kind = PasTypeKind::Interface;
        t.name = std::move(interfaceName);
        return t;
    }

    /// @brief Check if this is an optional type (T?).
    bool isOptional() const
    {
        return kind == PasTypeKind::Optional;
    }

    /// @brief Unwrap an optional type to get the inner type.
    /// @return The inner type if this is optional, or *this if not optional.
    PasType unwrap() const
    {
        if (kind == PasTypeKind::Optional && innerType)
            return *innerType;
        return *this;
    }

    /// @brief Make this type optional (T -> T?).
    /// @return A new optional type wrapping this type.
    static PasType makeOptional(const PasType &t)
    {
        // Already optional - don't double-wrap
        if (t.kind == PasTypeKind::Optional)
            return t;
        return optional(t);
    }

    /// @brief Check if this is a non-optional reference type (class/interface).
    /// @details Non-optional reference types cannot be assigned nil.
    bool isNonOptionalReference() const
    {
        return (kind == PasTypeKind::Class || kind == PasTypeKind::Interface) && !isOptional();
    }

    /// @brief Check if this type requires definite assignment before use.
    /// @details Non-optional class/interface locals must be definitely assigned before reading.
    bool requiresDefiniteAssignment() const
    {
        return isNonOptionalReference();
    }

    /// @brief Check if this is a numeric type (Integer or Real).
    bool isNumeric() const
    {
        return kind == PasTypeKind::Integer || kind == PasTypeKind::Real;
    }

    /// @brief Check if this is an ordinal type (Integer, Boolean, Enum, Range).
    bool isOrdinal() const
    {
        return kind == PasTypeKind::Integer || kind == PasTypeKind::Boolean ||
               kind == PasTypeKind::Enum || kind == PasTypeKind::Range;
    }

    /// @brief Check if this is a reference type (Class, Interface, dynamic Array, String).
    bool isReference() const
    {
        return kind == PasTypeKind::Class || kind == PasTypeKind::Interface ||
               kind == PasTypeKind::String || (kind == PasTypeKind::Array && dimensions == 0);
    }

    /// @brief Check if this is a value type (Integer, Real, Boolean, Enum, Record, fixed Array).
    /// @details Value types need (hasValue, value) pair representation when optional.
    bool isValueType() const
    {
        return kind == PasTypeKind::Integer || kind == PasTypeKind::Real ||
               kind == PasTypeKind::Boolean || kind == PasTypeKind::Enum ||
               kind == PasTypeKind::Record || (kind == PasTypeKind::Array && dimensions > 0);
    }

    /// @brief For optional types, check if inner type is a value type.
    bool isValueTypeOptional() const
    {
        if (kind != PasTypeKind::Optional || !innerType)
            return false;
        return innerType->isValueType();
    }

    /// @brief Check if nil can be assigned to this type.
    /// @details Per spec: nil can be assigned to T?, pointers, and dynamic arrays.
    ///          Non-optional class/interface types do NOT accept nil assignment.
    bool isNilAssignable() const
    {
        // Optional types always accept nil
        if (kind == PasTypeKind::Optional)
            return true;

        // Pointers accept nil
        if (kind == PasTypeKind::Pointer)
            return true;

        // Dynamic arrays accept nil
        if (kind == PasTypeKind::Array && dimensions == 0)
            return true;

        // Non-optional class/interface do NOT accept nil (per spec)
        // They require definite assignment before use
        return false;
    }

    /// @brief Check if this is an error/unknown type.
    bool isError() const
    {
        return kind == PasTypeKind::Unknown;
    }

    /// @brief Get a string representation of this type for diagnostics.
    std::string toString() const;
};

//===----------------------------------------------------------------------===//
// Function Signature
//===----------------------------------------------------------------------===//

/// @brief Signature for a procedure or function.
struct FuncSignature
{
    std::string name;                                    ///< Procedure/function name
    std::vector<std::pair<std::string, PasType>> params; ///< Parameter name-type pairs
    std::vector<bool> isVarParam;                        ///< Whether each param is var/out
    std::vector<bool> hasDefault;                        ///< Whether each param has a default value
    PasType returnType;                                  ///< Return type (Void for procedures)
    bool isForward{false};                               ///< Forward declaration?
    size_t requiredParams{0}; ///< Number of required (non-default) params
};

//===----------------------------------------------------------------------===//
// Constant Value
//===----------------------------------------------------------------------===//

/// @brief Constant value type for compile-time constant folding and unit exports.
/// Supports integer, real, string, and boolean constants.
struct ConstantValue
{
    PasType type;
    int64_t intVal{0};
    double realVal{0.0};
    std::string strVal;
    bool boolVal{false};
    bool hasValue{false}; ///< True if we have the actual value

    /// @brief Create an integer constant.
    static ConstantValue makeInt(int64_t val)
    {
        ConstantValue cv;
        cv.type = PasType::integer();
        cv.intVal = val;
        cv.hasValue = true;
        return cv;
    }

    /// @brief Create a real constant.
    static ConstantValue makeReal(double val)
    {
        ConstantValue cv;
        cv.type = PasType::real();
        cv.realVal = val;
        cv.hasValue = true;
        return cv;
    }

    /// @brief Create a string constant.
    static ConstantValue makeString(const std::string &val)
    {
        ConstantValue cv;
        cv.type = PasType::string();
        cv.strVal = val;
        cv.hasValue = true;
        return cv;
    }

    /// @brief Create a boolean constant.
    static ConstantValue makeBool(bool val)
    {
        ConstantValue cv;
        cv.type = PasType::boolean();
        cv.boolVal = val;
        cv.hasValue = true;
        return cv;
    }
};

} // namespace il::frontends::pascal
