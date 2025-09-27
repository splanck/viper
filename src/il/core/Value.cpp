// File: src/il/core/Value.cpp
// Purpose: Implements helpers for IL values.
// Key invariants: None.
// Ownership/Lifetime: Values are owned by their users.
// Links: docs/il-guide.md#reference

#include "il/core/Value.hpp"

#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace il::core
{

Value Value::temp(unsigned t)
{
    return Value{Kind::Temp, 0, 0.0, t, ""};
}

Value Value::constInt(long long v)
{
    return Value{Kind::ConstInt, v, 0.0, 0, ""};
}

Value Value::constFloat(double v)
{
    return Value{Kind::ConstFloat, 0, v, 0, ""};
}

Value Value::constStr(std::string s)
{
    return Value{Kind::ConstStr, 0, 0.0, 0, std::move(s)};
}

Value Value::global(std::string s)
{
    return Value{Kind::GlobalAddr, 0, 0.0, 0, std::move(s)};
}

Value Value::null()
{
    return Value{Kind::NullPtr, 0, 0.0, 0, ""};
}

std::string toString(const Value &v)
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
            if (s.find_first_of(".eE") == std::string::npos)
                s += ".0";
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
