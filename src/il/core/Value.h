#pragma once
#include <string>

namespace il::core {

/// @brief Tagged value used as operands and results in IL.
struct Value {
  enum class Kind { Temp, ConstInt, ConstFloat, ConstStr, GlobalAddr, NullPtr };
  Kind kind;
  long long i64{0};
  double f64{0.0};
  unsigned id{0};
  std::string str;

  static Value temp(unsigned t) { return Value{Kind::Temp, 0, 0.0, t, ""}; }
  static Value constInt(long long v) { return Value{Kind::ConstInt, v, 0.0, 0, ""}; }
  static Value constFloat(double v) { return Value{Kind::ConstFloat, 0, v, 0, ""}; }
  static Value constStr(std::string s) { return Value{Kind::ConstStr, 0, 0.0, 0, std::move(s)}; }
  static Value global(std::string s) { return Value{Kind::GlobalAddr, 0, 0.0, 0, std::move(s)}; }
  static Value null() { return Value{Kind::NullPtr, 0, 0.0, 0, ""}; }
};

inline std::string toString(const Value &v) {
  switch (v.kind) {
  case Value::Kind::Temp:
    return "%t" + std::to_string(v.id);
  case Value::Kind::ConstInt:
    return std::to_string(v.i64);
  case Value::Kind::ConstFloat:
    return std::to_string(v.f64);
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
