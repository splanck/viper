//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_marshal_constants.cpp
// Purpose: Test constant scalar conversion helpers in Marshal.cpp.
// Key invariants: toI64/toF64 only accept constant scalars (ConstInt, ConstFloat, NullPtr).
// Ownership/Lifetime: No runtime resources allocated.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "il/core/Value.hpp"
#include "vm/Marshal.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

int main()
{
    using il::core::Value;
    using Kind = Value::Kind;

    //===------------------------------------------------------------------===//
    // Test isConstantScalar predicate
    //===------------------------------------------------------------------===//

    // Constant scalar kinds should return true
    assert(il::vm::isConstantScalar(Kind::ConstInt));
    assert(il::vm::isConstantScalar(Kind::ConstFloat));
    assert(il::vm::isConstantScalar(Kind::NullPtr));

    // Non-constant kinds should return false
    assert(!il::vm::isConstantScalar(Kind::Temp));
    assert(!il::vm::isConstantScalar(Kind::ConstStr));
    assert(!il::vm::isConstantScalar(Kind::GlobalAddr));

    // Test the Value overload
    assert(il::vm::isConstantScalar(Value::constInt(42)));
    assert(il::vm::isConstantScalar(Value::constFloat(3.14)));
    assert(il::vm::isConstantScalar(Value::null()));
    assert(!il::vm::isConstantScalar(Value::temp(0)));
    assert(!il::vm::isConstantScalar(Value::constStr("hello")));
    assert(!il::vm::isConstantScalar(Value::global("my_global")));

    //===------------------------------------------------------------------===//
    // Test toI64 conversions for valid constant kinds
    //===------------------------------------------------------------------===//

    // ConstInt: direct conversion
    {
        Value v = Value::constInt(42);
        assert(il::vm::toI64(v) == 42);
    }
    {
        Value v = Value::constInt(-1000);
        assert(il::vm::toI64(v) == -1000);
    }
    {
        Value v = Value::constInt(0);
        assert(il::vm::toI64(v) == 0);
    }

    // ConstInt: large values
    {
        Value v = Value::constInt(std::numeric_limits<int64_t>::max());
        assert(il::vm::toI64(v) == std::numeric_limits<int64_t>::max());
    }
    {
        Value v = Value::constInt(std::numeric_limits<int64_t>::min());
        assert(il::vm::toI64(v) == std::numeric_limits<int64_t>::min());
    }

    // ConstFloat: truncation to integer
    {
        Value v = Value::constFloat(3.7);
        assert(il::vm::toI64(v) == 3); // truncates toward zero
    }
    {
        Value v = Value::constFloat(-3.7);
        assert(il::vm::toI64(v) == -3); // truncates toward zero
    }
    {
        Value v = Value::constFloat(0.0);
        assert(il::vm::toI64(v) == 0);
    }
    {
        Value v = Value::constFloat(100.999);
        assert(il::vm::toI64(v) == 100);
    }

    // NullPtr: always zero
    {
        Value v = Value::null();
        assert(il::vm::toI64(v) == 0);
    }

    //===------------------------------------------------------------------===//
    // Test toF64 conversions for valid constant kinds
    //===------------------------------------------------------------------===//

    // ConstFloat: direct access
    {
        Value v = Value::constFloat(3.14159);
        assert(il::vm::toF64(v) == 3.14159);
    }
    {
        Value v = Value::constFloat(-2.71828);
        assert(il::vm::toF64(v) == -2.71828);
    }
    {
        Value v = Value::constFloat(0.0);
        assert(il::vm::toF64(v) == 0.0);
    }

    // ConstFloat: special values
    {
        Value v = Value::constFloat(std::numeric_limits<double>::infinity());
        assert(std::isinf(il::vm::toF64(v)));
    }
    {
        Value v = Value::constFloat(-std::numeric_limits<double>::infinity());
        assert(std::isinf(il::vm::toF64(v)) && il::vm::toF64(v) < 0);
    }
    {
        Value v = Value::constFloat(std::numeric_limits<double>::quiet_NaN());
        assert(std::isnan(il::vm::toF64(v)));
    }

    // ConstInt: conversion to double
    {
        Value v = Value::constInt(42);
        assert(il::vm::toF64(v) == 42.0);
    }
    {
        Value v = Value::constInt(-1000);
        assert(il::vm::toF64(v) == -1000.0);
    }
    {
        Value v = Value::constInt(0);
        assert(il::vm::toF64(v) == 0.0);
    }

    // ConstInt: large values (may lose precision in double)
    {
        Value v = Value::constInt(1LL << 52);
        // Exact representation in double's 53-bit mantissa
        assert(il::vm::toF64(v) == static_cast<double>(1LL << 52));
    }

    // NullPtr: always zero
    {
        Value v = Value::null();
        assert(il::vm::toF64(v) == 0.0);
    }

    //===------------------------------------------------------------------===//
    // Verify precondition checking is documented and in place
    //===------------------------------------------------------------------===//
    //
    // The following would trigger assertions and abort:
    //   - toI64(Value::temp(0))
    //   - toI64(Value::constStr("x"))
    //   - toI64(Value::global("g"))
    //   - toF64(Value::temp(0))
    //   - toF64(Value::constStr("x"))
    //   - toF64(Value::global("g"))
    //
    // We cannot test these at runtime without aborting the process.
    // The isConstantScalar predicate allows callers to check before calling.
    // In debug builds, assertions catch programmer errors immediately.
    // In release builds, std::abort() is called with a diagnostic message.

    //===------------------------------------------------------------------===//
    // Constexpr check: isConstantScalar is usable at compile time
    //===------------------------------------------------------------------===//
    static_assert(il::vm::isConstantScalar(Kind::ConstInt), "ConstInt should be constant scalar");
    static_assert(il::vm::isConstantScalar(Kind::ConstFloat),
                  "ConstFloat should be constant scalar");
    static_assert(il::vm::isConstantScalar(Kind::NullPtr), "NullPtr should be constant scalar");
    static_assert(!il::vm::isConstantScalar(Kind::Temp), "Temp should not be constant scalar");
    static_assert(!il::vm::isConstantScalar(Kind::ConstStr),
                  "ConstStr should not be constant scalar");
    static_assert(!il::vm::isConstantScalar(Kind::GlobalAddr),
                  "GlobalAddr should not be constant scalar");

    return 0;
}
