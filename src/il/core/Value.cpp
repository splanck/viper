//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the helper constructors and formatting routines that accompany the
// lightweight IL value type.  Callers primarily use these helpers when building
// or serialising IR, so concentrating the logic here keeps the header concise
// while documenting the canonical encodings for temporaries and constants.
//
//===----------------------------------------------------------------------===//

#include "il/core/Value.hpp"
#include "il/io/StringEscape.hpp"

#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace il::core
{

/// @brief Create a temporary value wrapper for SSA identifiers.
///
/// Temporaries appear as `%tN` in the textual IL.  They always use the temp
/// kind and record the numeric identifier in @ref Value::id.
///
/// @param t Zero-based identifier assigned by the surrounding builder.
/// @return Value representing the temporary reference.
Value Value::temp(unsigned t)
{
    return Value{Kind::Temp, 0, 0.0, t, ""};
}

/// @brief Create a signed integer constant value.
///
/// The payload preserves the exact @c long long bit pattern so two's-complement
/// wraparound semantics are preserved when consumed by handlers.
///
/// @param v Integer payload to embed in the value.
/// @return Value representing the integer literal.
Value Value::constInt(long long v)
{
    return Value{Kind::ConstInt, v, 0.0, 0, ""};
}

/// @brief Create a boolean literal backed by the integer constant encoding.
///
/// Booleans piggy-back on the integer constant representation but set the
/// @ref Value::isBool flag so printers render them as `true` / `false` instead
/// of numeric digits.
///
/// @param v Boolean payload to embed in the value.
/// @return Value representing the boolean literal.
Value Value::constBool(bool v)
{
    return Value{Kind::ConstInt, v ? 1 : 0, 0.0, 0, "", true};
}

/// @brief Create a floating-point constant value.
///
/// Stores the exact @c double payload so NaNs and infinities propagate through
/// the IR unchanged.
///
/// @param v Floating-point payload to embed in the value.
/// @return Value representing the floating literal.
Value Value::constFloat(double v)
{
    return Value{Kind::ConstFloat, 0, v, 0, ""};
}

/// @brief Create a string literal value.
///
/// The string payload is owned by the @ref Value instance and therefore moved
/// into place.  Literal encoding (escaped or raw) is handled by callers.
///
/// @param s String contents of the literal.
/// @return Value representing the string literal.
Value Value::constStr(std::string s)
{
    return Value{Kind::ConstStr, 0, 0.0, 0, std::move(s)};
}

/// @brief Create a global address value that refers to a named global symbol.
///
/// The stored string is the canonical name of the global and is owned by the
/// resulting @ref Value instance.
///
/// @param s Name of the referenced global.
/// @return Value representing the global address literal.
Value Value::global(std::string s)
{
    return Value{Kind::GlobalAddr, 0, 0.0, 0, std::move(s)};
}

/// @brief Create the null pointer literal used by pointer-typed values.
///
/// Null values always carry the @ref Kind::NullPtr tag and have empty payloads.
///
/// @return Value representing the null literal.
Value Value::null()
{
    return Value{Kind::NullPtr, 0, 0.0, 0, ""};
}

/// @brief Render a value into its textual IL representation.
///
/// The printer mirrors the canonical format produced by the serializer: temps
/// appear as `%tN`, integers as decimal literals (with booleans spelled out),
/// floating-point values use a trimmed scientific/decimal format, strings are
/// escaped, and globals are prefixed with `@`.  Null pointers render as `null`.
///
/// @param v Value to render.
/// @return String representation suitable for diagnostics or textual IL.
std::string toString(const Value &v)
{
    switch (v.kind)
    {
        case Value::Kind::Temp:
            return "%t" + std::to_string(v.id);
        case Value::Kind::ConstInt:
            if (v.isBool)
                return v.i64 != 0 ? "true" : "false";
            return std::to_string(v.i64);
        case Value::Kind::ConstFloat:
        {
            std::ostringstream oss;
            oss.setf(std::ios::fmtflags(0), std::ios::floatfield);
            oss << std::setprecision(std::numeric_limits<double>::digits10 + 2) << v.f64;
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
            return std::string("\"") + il::io::encodeEscapedString(v.str) + "\"";
        case Value::Kind::GlobalAddr:
            return "@" + v.str;
        case Value::Kind::NullPtr:
            return "null";
    }
    return "";
}

} // namespace il::core
