// File: src/il/core/Value.cpp
// Purpose: Implements helpers for IL values.
// Key invariants: None.
// Ownership/Lifetime: Values are owned by their users.
// Links: docs/il-spec.md

#include "il/core/Value.hpp"

#include <iomanip>
#include <limits>
#include <sstream>

namespace il::core
{

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
