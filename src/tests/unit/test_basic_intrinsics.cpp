//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_intrinsics.cpp
// Purpose: Unit test for BASIC intrinsic registry lookups.
// Key invariants: Verifies parameter counts/types for selected intrinsics.
// Ownership/Lifetime: Test owns nothing.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Intrinsics.hpp"
#include <cassert>

using namespace il::frontends::basic::intrinsics;

int main()
{
    const Intrinsic *i = lookup("LEFT$");
    assert(i && i->returnType == Type::String && i->paramCount == 2);
    assert(i->params[0].type == Type::String && !i->params[0].optional);
    assert(i->params[1].type == Type::Int && !i->params[1].optional);

    i = lookup("MID$");
    assert(i && i->paramCount == 3 && i->params[2].optional);

    i = lookup("INSTR");
    assert(i && i->returnType == Type::Int && i->paramCount == 3);
    assert(i->params[0].optional && i->params[0].type == Type::Int);

    i = lookup("STR$");
    assert(i && i->params[0].type == Type::Numeric);

    assert(lookup("NOPE") == nullptr);
    return 0;
}
