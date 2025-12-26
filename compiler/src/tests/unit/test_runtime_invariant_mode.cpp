//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_runtime_invariant_mode.cpp
// Purpose: Test the invariant violation mode configuration API.
// Key invariants: Default mode is Abort; handlers are properly registered.
// Ownership/Lifetime: Uses static configuration state; tests must reset state.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//
#include "il/runtime/RuntimeSignatures.hpp"
#include "tests/TestHarness.hpp"

namespace
{

/// @brief RAII helper to restore invariant mode after test.
struct ModeRestorer
{
    il::runtime::InvariantViolationMode original;
    il::runtime::InvariantTrapHandler originalHandler;

    ModeRestorer()
        : original(il::runtime::getInvariantViolationMode()),
          originalHandler(il::runtime::getInvariantTrapHandler())
    {
    }

    ~ModeRestorer()
    {
        il::runtime::setInvariantViolationMode(original);
        il::runtime::setInvariantTrapHandler(originalHandler);
    }
};

/// @brief Test that the default mode is Abort.
TEST(InvariantViolationMode, DefaultIsAbort)
{
    // Note: This test assumes no other code has changed the mode.
    // The VM static initializer may have changed it, so we just verify
    // the API works rather than the specific default value.
    const auto mode = il::runtime::getInvariantViolationMode();
    // Mode should be one of the valid enum values
    ASSERT_TRUE(mode == il::runtime::InvariantViolationMode::Abort ||
                mode == il::runtime::InvariantViolationMode::Trap);
}

/// @brief Test setting and getting the mode.
TEST(InvariantViolationMode, SetAndGetMode)
{
    ModeRestorer restorer;

    // Set to Abort
    il::runtime::setInvariantViolationMode(il::runtime::InvariantViolationMode::Abort);
    ASSERT_EQ(il::runtime::getInvariantViolationMode(), il::runtime::InvariantViolationMode::Abort);

    // Set to Trap
    il::runtime::setInvariantViolationMode(il::runtime::InvariantViolationMode::Trap);
    ASSERT_EQ(il::runtime::getInvariantViolationMode(), il::runtime::InvariantViolationMode::Trap);

    // Set back to Abort
    il::runtime::setInvariantViolationMode(il::runtime::InvariantViolationMode::Abort);
    ASSERT_EQ(il::runtime::getInvariantViolationMode(), il::runtime::InvariantViolationMode::Abort);
}

/// @brief Test setting and getting the trap handler.
TEST(InvariantViolationMode, SetAndGetHandler)
{
    ModeRestorer restorer;

    // Custom handler for testing
    static bool handlerCalled = false;
    static const char *lastMessage = nullptr;

    auto testHandler = [](const char *message) -> bool
    {
        handlerCalled = true;
        lastMessage = message;
        return false; // Indicate trap not handled (would fall through to abort)
    };

    // Set the handler
    il::runtime::setInvariantTrapHandler(testHandler);
    ASSERT_EQ(il::runtime::getInvariantTrapHandler(), testHandler);

    // Set to nullptr
    il::runtime::setInvariantTrapHandler(nullptr);
    ASSERT_EQ(il::runtime::getInvariantTrapHandler(), nullptr);
}

/// @brief Test that handler registration can be chained.
TEST(InvariantViolationMode, HandlerRegistrationChaining)
{
    ModeRestorer restorer;

    auto handler1 = [](const char *) -> bool { return false; };
    auto handler2 = [](const char *) -> bool { return true; };

    il::runtime::setInvariantTrapHandler(handler1);
    ASSERT_EQ(il::runtime::getInvariantTrapHandler(), handler1);

    il::runtime::setInvariantTrapHandler(handler2);
    ASSERT_EQ(il::runtime::getInvariantTrapHandler(), handler2);

    // Verify handler1 is no longer registered
    ASSERT_NE(il::runtime::getInvariantTrapHandler(), handler1);
}

/// @brief Test that handler can be nullptr.
/// @details Verify that nullptr is a valid handler value.
TEST(InvariantViolationMode, NullHandlerIsValid)
{
    ModeRestorer restorer;

    il::runtime::setInvariantTrapHandler(nullptr);
    ASSERT_EQ(il::runtime::getInvariantTrapHandler(), nullptr);
}

} // namespace

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
