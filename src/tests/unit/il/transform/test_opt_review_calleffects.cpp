//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for CallEffects fixes from the IL optimization review:
// - Early return when fully classified (skip O(n) registry scan)
// - Correct classification from instruction attributes
// - Non-call instructions return conservative default
//
//===----------------------------------------------------------------------===//

#include "il/transform/CallEffects.hpp"

#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "tests/TestHarness.hpp"

using namespace il::core;

namespace
{

Instr makeCall(std::string callee)
{
    Instr instr;
    instr.op = Opcode::Call;
    instr.callee = std::move(callee);
    return instr;
}

} // namespace

// Test that instruction-level pure attribute is detected
TEST(CallEffects, InstrPureAttribute)
{
    Instr call = makeCall("unknown_fn");
    call.CallAttr.pure = true;

    auto effects = il::transform::classifyCallEffects(call);
    EXPECT_TRUE(effects.pure);
    EXPECT_TRUE(effects.canEliminateIfUnused());
}

// Test that instruction-level readonly attribute is detected
TEST(CallEffects, InstrReadonlyAttribute)
{
    Instr call = makeCall("unknown_fn");
    call.CallAttr.readonly = true;

    auto effects = il::transform::classifyCallEffects(call);
    EXPECT_TRUE(effects.readonly);
    EXPECT_TRUE(effects.canReorderWithMemory());
}

// Test that non-call instructions get conservative classification
TEST(CallEffects, NonCallIsConservative)
{
    Instr load;
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::I64);

    auto effects = il::transform::classifyCallEffects(load);
    EXPECT_FALSE(effects.pure);
    EXPECT_FALSE(effects.readonly);
    EXPECT_FALSE(effects.nothrow);
    EXPECT_FALSE(effects.canEliminateIfUnused());
    EXPECT_FALSE(effects.canReorderWithMemory());
}

// Test that pure + readonly = canReorderWithMemory
TEST(CallEffects, PureImpliesCanReorder)
{
    Instr call = makeCall("some_fn");
    call.CallAttr.pure = true;

    auto effects = il::transform::classifyCallEffects(call);
    EXPECT_TRUE(effects.canReorderWithMemory());
    EXPECT_TRUE(effects.canEliminateIfUnused());
}

// Test classifyCalleeEffects by name (string-based lookup)
TEST(CallEffects, ClassifyCalleeByName)
{
    // Unknown callee should return conservative classification
    auto effects = il::transform::classifyCalleeEffects("totally_unknown_function_xyz");
    // Unknown functions are not pure/readonly by default
    EXPECT_FALSE(effects.pure);
    EXPECT_FALSE(effects.readonly);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
