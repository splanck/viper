//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/core/test_Attrs.cpp
// Purpose: Validate attribute containers for functions, parameters, calls, and runtime signatures.
// Key invariants: Attribute setters/getters propagate state without affecting other metadata.
// Ownership/Lifetime: Tests instantiate transient IL structures on the stack.
// Links: src/il/core/Function.hpp, src/il/core/Param.hpp, src/il/core/Instr.hpp,
//
//===----------------------------------------------------------------------===//
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Param.hpp"
#include "il/runtime/signatures/Registry.hpp"
#include "tests/TestHarness.hpp"

using il::core::Function;
using il::core::Instr;
using il::core::Opcode;
using il::core::Param;
using il::runtime::signatures::make_signature;
using il::runtime::signatures::Signature;

TEST(Attrs, FunctionAttributesRoundTrip)
{
    Function fn;
    EXPECT_FALSE(fn.attrs().nothrow);
    EXPECT_FALSE(fn.attrs().readonly);
    EXPECT_FALSE(fn.attrs().pure);

    fn.attrs().nothrow = true;
    fn.attrs().readonly = true;
    fn.attrs().pure = false;

    EXPECT_TRUE(fn.attrs().nothrow);
    EXPECT_TRUE(fn.attrs().readonly);
    EXPECT_FALSE(fn.attrs().pure);
}

TEST(Attrs, ParamAttributesSetters)
{
    Param param;
    EXPECT_FALSE(param.isNoAlias());
    EXPECT_FALSE(param.isNoCapture());
    EXPECT_FALSE(param.isNonNull());

    param.setNoAlias(true);
    param.setNoCapture(true);
    param.setNonNull(true);

    EXPECT_TRUE(param.isNoAlias());
    EXPECT_TRUE(param.isNoCapture());
    EXPECT_TRUE(param.isNonNull());

    param.setNoAlias(false);
    param.setNoCapture(false);
    param.setNonNull(false);

    EXPECT_FALSE(param.isNoAlias());
    EXPECT_FALSE(param.isNoCapture());
    EXPECT_FALSE(param.isNonNull());
}

TEST(Attrs, CallInstructionAttributes)
{
    Instr call;
    call.op = Opcode::Call;

    EXPECT_FALSE(call.CallAttr.nothrow);
    EXPECT_FALSE(call.CallAttr.readonly);
    EXPECT_FALSE(call.CallAttr.pure);

    call.CallAttr.nothrow = true;
    call.CallAttr.readonly = true;
    call.CallAttr.pure = true;

    EXPECT_TRUE(call.CallAttr.nothrow);
    EXPECT_TRUE(call.CallAttr.readonly);
    EXPECT_TRUE(call.CallAttr.pure);
}

TEST(Attrs, RuntimeSignatureAttributeConstruction)
{
    Signature sig = make_signature("rt_probe", {}, {}, true, true, false);

    EXPECT_TRUE(sig.nothrow);
    EXPECT_TRUE(sig.readonly);
    EXPECT_FALSE(sig.pure);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
