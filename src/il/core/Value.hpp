// File: src/il/core/Value.hpp
// Purpose: Defines IL value variants.
// Key invariants: Discriminant matches stored payload.
// Ownership/Lifetime: Values are passed by value.
// Links: docs/il-spec.md
#pragma once

#include <string>
#include <utility>

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

    /// @brief Construct a temporary value.
    /// @param t Identifier of the temporary.
    /// @return Value with kind Kind::Temp and id set to t.
    /// @invariant result.kind == Kind::Temp.
    static Value temp(unsigned t)
    {
        return Value{Kind::Temp, 0, 0.0, t, ""};
    }

    /// @brief Construct an integer constant value.
    /// @param v Signed integer literal.
    /// @return Value with kind Kind::ConstInt and i64 set to v.
    /// @invariant result.kind == Kind::ConstInt.
    static Value constInt(long long v)
    {
        return Value{Kind::ConstInt, v, 0.0, 0, ""};
    }

    /// @brief Construct a floating-point constant value.
    /// @param v IEEE-754 double literal.
    /// @return Value with kind Kind::ConstFloat and f64 set to v.
    /// @invariant result.kind == Kind::ConstFloat.
    static Value constFloat(double v)
    {
        return Value{Kind::ConstFloat, 0, v, 0, ""};
    }

    /// @brief Construct a string constant value.
    /// @param s String literal; moved into the value.
    /// @return Value with kind Kind::ConstStr and str set to @p s.
    /// @invariant result.kind == Kind::ConstStr.
    static Value constStr(std::string s)
    {
        return Value{Kind::ConstStr, 0, 0.0, 0, std::move(s)};
    }

    /// @brief Construct a global address value.
    /// @param s Name of the global symbol; moved into the value.
    /// @return Value with kind Kind::GlobalAddr and str set to @p s.
    /// @invariant result.kind == Kind::GlobalAddr.
    static Value global(std::string s)
    {
        return Value{Kind::GlobalAddr, 0, 0.0, 0, std::move(s)};
    }

    /// @brief Construct a null pointer value.
    /// @return Value with kind Kind::NullPtr.
    /// @invariant result.kind == Kind::NullPtr.
    static Value null()
    {
        return Value{Kind::NullPtr, 0, 0.0, 0, ""};
    }
};

std::string toString(const Value &v);

} // namespace il::core
