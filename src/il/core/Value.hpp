// File: src/il/core/Value.hpp
// Purpose: Defines IL value variants.
// Key invariants: Discriminant matches stored payload.
// Ownership/Lifetime: Values are passed by value.
// Links: docs/il-spec.md
#pragma once

#include <iomanip>
#include <limits>
#include <sstream>
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

inline std::string toString(const Value &v)
{
    switch (v.kind)
    {
        case Value::Kind::Temp:
            return "%t" + std::to_string(v.id);
        case Value::Kind::ConstInt:
            return std::to_string(v.i64);
        case Value::Kind::ConstFloat:
        {
            std::ostringstream oss;
            oss.setf(std::ios::fmtflags(0), std::ios::floatfield);
            oss << std::setprecision(std::numeric_limits<double>::digits10 + 1) << v.f64;
            std::string s = oss.str();
            if (v.f64 == 0.0)
                return "0.0";
            if (s.find('.') != std::string::npos)
            {
                while (!s.empty() && s.back() == '0')
                    s.pop_back();
                if (!s.empty() && s.back() == '.')
                    s.pop_back();
            }
            return s;
        }
        case Value::Kind::ConstStr:
            return "\"" + v.str + "\"";
        case Value::Kind::GlobalAddr:
            return "@" + v.str;
        case Value::Kind::NullPtr:
            return "null";
    }
    return "";
}

} // namespace il::core
