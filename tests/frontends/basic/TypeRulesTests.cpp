// File: tests/frontends/basic/TypeRulesTests.cpp
// Purpose: Validate BASIC numeric type promotion and operator result helpers.
// Key invariants: TypeRules provides deterministic lattice-based results.
// Ownership/Lifetime: Standalone executable using simple assertions.
// Links: docs/codemap.md

#include "frontends/basic/TypeRules.hpp"

#include <cassert>

using il::frontends::basic::TypeRules;

int main()
{
    using NumericType = TypeRules::NumericType;

    assert(TypeRules::resultType('/', NumericType::Integer, NumericType::Integer) ==
           NumericType::Double);
    assert(TypeRules::resultType('/', NumericType::Single, NumericType::Integer) ==
           NumericType::Single);
    assert(TypeRules::resultType('\\', NumericType::Integer, NumericType::Long) ==
           NumericType::Long);
    assert(TypeRules::resultType("MOD", NumericType::Long, NumericType::Integer) ==
           NumericType::Long);
    assert(TypeRules::resultType('^', NumericType::Single, NumericType::Single) ==
           NumericType::Double);

    assert(TypeRules::unaryResultType('-', NumericType::Integer) == NumericType::Integer);
    assert(TypeRules::unaryResultType('+', NumericType::Double) == NumericType::Double);

    return 0;
}
