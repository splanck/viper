//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for CallEffects fixes from the IL optimization review:
// - Early return when fully classified (skip O(n) registry scan)
// - Unknown call-site attributes remain conservative without metadata
// - Non-call instructions return conservative default
//
//===----------------------------------------------------------------------===//

#include "il/transform/CallEffects.hpp"

#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "tests/TestHarness.hpp"

using namespace il::core;

namespace {

/// @brief Make call.
Instr makeCall(std::string callee) {
    Instr instr;
    instr.op = Opcode::Call;
    instr.callee = std::move(callee);
    return instr;
}

} // namespace

TEST(CallEffects, UnknownInstrPureAttributeIsConservative) {
    Instr call = makeCall("unknown_fn");
    call.CallAttr.pure = true;

    auto effects = il::transform::classifyCallEffects(call);
    EXPECT_FALSE(effects.pure);
    EXPECT_FALSE(effects.canEliminateIfUnused());
}

TEST(CallEffects, UnknownInstrPureNothrowCannotEliminate) {
    Instr call = makeCall("unknown_fn");
    call.CallAttr.pure = true;
    call.CallAttr.nothrow = true;

    auto effects = il::transform::classifyCallEffects(call);
    EXPECT_FALSE(effects.pure);
    EXPECT_FALSE(effects.nothrow);
    EXPECT_FALSE(effects.canEliminateIfUnused());
}

TEST(CallEffects, UnknownInstrReadonlyAttributeIsConservative) {
    Instr call = makeCall("unknown_fn");
    call.CallAttr.readonly = true;

    auto effects = il::transform::classifyCallEffects(call);
    EXPECT_FALSE(effects.readonly);
    EXPECT_FALSE(effects.canReorderWithMemory());
}

// Test that non-call instructions get conservative classification
TEST(CallEffects, NonCallIsConservative) {
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

TEST(CallEffects, UnknownPureAttributeDoesNotImplyCanReorder) {
    Instr call = makeCall("some_fn");
    call.CallAttr.pure = true;

    auto effects = il::transform::classifyCallEffects(call);
    EXPECT_FALSE(effects.canReorderWithMemory());
    EXPECT_FALSE(effects.canEliminateIfUnused());
}

// Test classifyCalleeEffects by name (string-based lookup)
TEST(CallEffects, ClassifyCalleeByName) {
    // Unknown callee should return conservative classification
    auto effects = il::transform::classifyCalleeEffects("totally_unknown_function_xyz");
    // Unknown functions are not pure/readonly by default
    EXPECT_FALSE(effects.pure);
    EXPECT_FALSE(effects.readonly);
}

TEST(CallEffects, GeneratedRuntimeMetadataIsAuthoritative) {
    Instr call = makeCall("rt_round_even");
    call.CallAttr.pure = false;
    call.CallAttr.nothrow = false;

    auto effects = il::transform::classifyCallEffects(call);
    EXPECT_TRUE(effects.pure);
    EXPECT_TRUE(effects.nothrow);
    EXPECT_TRUE(effects.canEliminateIfUnused());
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
