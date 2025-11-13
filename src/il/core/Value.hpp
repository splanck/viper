//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Value struct, which represents operands and constants
// in IL instructions. Values are tagged unions that can hold temporaries, literal
// constants, global addresses, or null pointers.
//
// The Value type is central to IL's SSA representation. Instructions produce
// values (stored in temporaries) and consume values (from operands). The tagged
// union design allows a single Value type to represent all possible operand
// forms without polymorphism or heap allocation.
//
// Supported Value Kinds:
// - Temp: SSA temporary reference (%0, %1, etc.)
// - ConstInt: Integer literal (i16, i32, i64)
// - ConstFloat: Floating-point literal (f64)
// - ConstStr: String literal ("hello")
// - GlobalAddr: Address of global symbol (@varName)
// - NullPtr: Null pointer constant
//
// The Value struct uses a discriminated union pattern with a Kind enum field
// that determines which payload field is active. Factory methods (temp, constInt,
// etc.) construct Values with the appropriate kind and payload, ensuring type
// safety at the API level.
//
// Special Handling for Booleans:
// Integer constants can represent both numeric integers and i1 boolean values.
// The isBool flag distinguishes between these interpretations, affecting
// serialization and type checking.
//
// Design Rationale:
// - Value semantics: Values are lightweight POD structs suitable for copying
// - No polymorphism: Avoids virtual dispatch and heap allocation overhead
// - Factory methods: Prevent construction of invalid discriminant/payload pairs
// - String storage: Used for both string constants and global symbol names
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

namespace il::core
{

/// @brief Tagged value used as operands and results in IL.
struct Value
{
    /// @brief Enumerates the different value forms.
    enum class Kind
    {
        Temp,
        ConstInt,
        ConstFloat,
        ConstStr,
        GlobalAddr,
        NullPtr
    };
    /// Discriminant selecting which payload is active.
    Kind kind;
    /// Integer payload used when kind == Kind::ConstInt.
    long long i64{0};
    /// Floating-point payload used when kind == Kind::ConstFloat.
    double f64{0.0};
    /// Temporary identifier used when kind == Kind::Temp.
    unsigned id{0};
    /// String payload for string constants and global names.
    std::string str;

    /// @brief Flag set when the integer literal represents an i1 boolean.
    /// @invariant Only meaningful when kind == Kind::ConstInt.
    bool isBool{false};

    /// @brief Construct a temporary value.
    /// @param t Identifier of the temporary.
    /// @return Value with kind Kind::Temp and id set to t.
    /// @invariant result.kind == Kind::Temp.
    static Value temp(unsigned t);

    /// @brief Construct an integer constant value.
    /// @param v Signed integer literal.
    /// @return Value with kind Kind::ConstInt and i64 set to v.
    /// @invariant result.kind == Kind::ConstInt.
    static Value constInt(long long v);

    /// @brief Construct a boolean constant value.
    /// @param v Boolean literal to encode as an i1 constant.
    /// @return Value with kind Kind::ConstInt and boolean flag set.
    /// @invariant result.kind == Kind::ConstInt and result.isBool == true.
    static Value constBool(bool v);

    /// @brief Construct a floating-point constant value.
    /// @param v IEEE-754 double literal.
    /// @return Value with kind Kind::ConstFloat and f64 set to v.
    /// @invariant result.kind == Kind::ConstFloat.
    static Value constFloat(double v);

    /// @brief Construct a string constant value.
    /// @param s String literal; moved into the value.
    /// @return Value with kind Kind::ConstStr and str set to @p s.
    /// @invariant result.kind == Kind::ConstStr.
    static Value constStr(std::string s);

    /// @brief Construct a global address value.
    /// @param s Name of the global symbol; moved into the value.
    /// @return Value with kind Kind::GlobalAddr and str set to @p s.
    /// @invariant result.kind == Kind::GlobalAddr.
    static Value global(std::string s);

    /// @brief Construct a null pointer value.
    /// @return Value with kind Kind::NullPtr.
    /// @invariant result.kind == Kind::NullPtr.
    static Value null();
};

std::string toString(const Value &v);

} // namespace il::core
