// File: tests/runtime/FloatDeterminismTests.cpp
// Purpose: Exercise VAL-style parsing for locale-independent behavior.
// Key invariants: Special values and decimal formats are deterministic regardless of locale.
// Ownership: Runtime numeric helpers.
// Links: docs/codemap.md

#include "rt_numeric.h"

#include <cassert>
#include <cmath>

int main()
{
    bool ok = true;

    double nanValue = rt_val_to_double("NaN", &ok);
    assert(!ok);
    assert(std::isnan(nanValue));

    ok = true;
    double infValue = rt_val_to_double("Inf", &ok);
    assert(!ok);
    assert(std::isinf(infValue) && infValue > 0.0);

    ok = true;
    double negInfValue = rt_val_to_double("-Inf", &ok);
    assert(!ok);
    assert(std::isinf(negInfValue) && negInfValue < 0.0);

    ok = true;
    double decimalValue = rt_val_to_double("1.2345", &ok);
    assert(ok);
    assert(decimalValue == 1.2345);

    ok = true;
    double commaValue = rt_val_to_double("1,234", &ok);
    assert(!ok);
    assert(commaValue == 0.0);

    ok = true;
    double spacedNan = rt_val_to_double("   NaN", &ok);
    assert(!ok);
    assert(std::isnan(spacedNan));

    return 0;
}
