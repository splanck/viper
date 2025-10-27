// File: tests/unit/test_basic_lower_builtin_dispatch.cpp
// Purpose: Validate BASIC builtin lowering registers family handlers through the
//          shared dispatcher and exposes them via the builtin registry.
// Key invariants: Each builtin family shares the same handler pointer after
//                 registration and the registry produces non-null handlers for
//                 supported builtins.
// Ownership/Lifetime: Test owns the Lowerer instance and relies on the builtin
//                     registry's process lifetime.
// Links: docs/codemap.md

#include "GTestStub.hpp"
#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/lower/common/BuiltinUtils.hpp"

using il::frontends::basic::BuiltinCallExpr;
using il::frontends::basic::Lowerer;
using il::frontends::basic::lower::common::ensureBuiltinHandlersForTesting;

namespace
{

il::frontends::basic::BuiltinHandler findHandler(BuiltinCallExpr::Builtin builtin)
{
    return il::frontends::basic::find_builtin(il::frontends::basic::getBuiltinInfo(builtin).name);
}

} // namespace

TEST(BasicLowerBuiltinDispatch, RegistersFamilies)
{
    Lowerer lowerer;
    ensureBuiltinHandlersForTesting();

    auto lenHandler = findHandler(BuiltinCallExpr::Builtin::Len);
    ASSERT_NE(lenHandler, nullptr);
    EXPECT_EQ(lenHandler, findHandler(BuiltinCallExpr::Builtin::Mid));
    EXPECT_EQ(lenHandler, findHandler(BuiltinCallExpr::Builtin::Right));

    auto valHandler = findHandler(BuiltinCallExpr::Builtin::Val);
    ASSERT_NE(valHandler, nullptr);
    EXPECT_EQ(valHandler, findHandler(BuiltinCallExpr::Builtin::Cint));
    EXPECT_EQ(valHandler, findHandler(BuiltinCallExpr::Builtin::Csng));

    auto mathHandler = findHandler(BuiltinCallExpr::Builtin::Cdbl);
    ASSERT_NE(mathHandler, nullptr);
    EXPECT_EQ(mathHandler, findHandler(BuiltinCallExpr::Builtin::Round));
    EXPECT_EQ(mathHandler, findHandler(BuiltinCallExpr::Builtin::Pow));
    EXPECT_EQ(mathHandler, findHandler(BuiltinCallExpr::Builtin::Rnd));
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
