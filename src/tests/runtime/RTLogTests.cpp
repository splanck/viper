//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTLogTests.cpp
// Purpose: Tests for Viper.Log simple logging functions.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_log.h"
#include "rt_string.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

// ============================================================================
// Helper
// ============================================================================

static rt_string make_str(const char *s)
{
    return rt_const_cstr(s);
}

// ============================================================================
// Level Constant Tests
// ============================================================================

static void test_level_constants()
{
    assert(rt_log_level_debug() == 0);
    assert(rt_log_level_info() == 1);
    assert(rt_log_level_warn() == 2);
    assert(rt_log_level_error() == 3);
    assert(rt_log_level_off() == 4);

    printf("test_level_constants: PASSED\n");
}

// ============================================================================
// Level Get/Set Tests
// ============================================================================

static void test_level_get_set()
{
    // Save original level
    int64_t original = rt_log_level();

    // Test setting each level
    rt_log_set_level(rt_log_level_debug());
    assert(rt_log_level() == 0);

    rt_log_set_level(rt_log_level_info());
    assert(rt_log_level() == 1);

    rt_log_set_level(rt_log_level_warn());
    assert(rt_log_level() == 2);

    rt_log_set_level(rt_log_level_error());
    assert(rt_log_level() == 3);

    rt_log_set_level(rt_log_level_off());
    assert(rt_log_level() == 4);

    // Test clamping - below minimum
    rt_log_set_level(-1);
    assert(rt_log_level() == 0);

    // Test clamping - above maximum
    rt_log_set_level(100);
    assert(rt_log_level() == 4);

    // Restore original level
    rt_log_set_level(original);

    printf("test_level_get_set: PASSED\n");
}

// ============================================================================
// Enabled Tests
// ============================================================================

static void test_enabled()
{
    // Save original level
    int64_t original = rt_log_level();

    // At DEBUG level, all levels are enabled
    rt_log_set_level(rt_log_level_debug());
    assert(rt_log_enabled(rt_log_level_debug()) == true);
    assert(rt_log_enabled(rt_log_level_info()) == true);
    assert(rt_log_enabled(rt_log_level_warn()) == true);
    assert(rt_log_enabled(rt_log_level_error()) == true);

    // At INFO level, DEBUG is disabled
    rt_log_set_level(rt_log_level_info());
    assert(rt_log_enabled(rt_log_level_debug()) == false);
    assert(rt_log_enabled(rt_log_level_info()) == true);
    assert(rt_log_enabled(rt_log_level_warn()) == true);
    assert(rt_log_enabled(rt_log_level_error()) == true);

    // At WARN level, DEBUG and INFO are disabled
    rt_log_set_level(rt_log_level_warn());
    assert(rt_log_enabled(rt_log_level_debug()) == false);
    assert(rt_log_enabled(rt_log_level_info()) == false);
    assert(rt_log_enabled(rt_log_level_warn()) == true);
    assert(rt_log_enabled(rt_log_level_error()) == true);

    // At ERROR level, only ERROR is enabled
    rt_log_set_level(rt_log_level_error());
    assert(rt_log_enabled(rt_log_level_debug()) == false);
    assert(rt_log_enabled(rt_log_level_info()) == false);
    assert(rt_log_enabled(rt_log_level_warn()) == false);
    assert(rt_log_enabled(rt_log_level_error()) == true);

    // At OFF level, nothing is enabled
    rt_log_set_level(rt_log_level_off());
    assert(rt_log_enabled(rt_log_level_debug()) == false);
    assert(rt_log_enabled(rt_log_level_info()) == false);
    assert(rt_log_enabled(rt_log_level_warn()) == false);
    assert(rt_log_enabled(rt_log_level_error()) == false);
    assert(rt_log_enabled(rt_log_level_off()) == false);

    // Restore original level
    rt_log_set_level(original);

    printf("test_enabled: PASSED\n");
}

// ============================================================================
// Log Output Tests (visual inspection)
// ============================================================================

static void test_log_output()
{
    // Save original level
    int64_t original = rt_log_level();

    // Set to DEBUG so all messages are shown
    rt_log_set_level(rt_log_level_debug());

    printf("\n--- Visual inspection of log output (expect 4 lines to stderr) ---\n");
    fflush(stdout);

    rt_log_debug(make_str("This is a debug message"));
    rt_log_info(make_str("This is an info message"));
    rt_log_warn(make_str("This is a warning message"));
    rt_log_error(make_str("This is an error message"));

    printf("--- End of log output ---\n\n");

    // Test that disabled levels don't output
    printf("--- Setting level to ERROR (should see no output) ---\n");
    fflush(stdout);

    rt_log_set_level(rt_log_level_error());
    rt_log_debug(make_str("DEBUG - should NOT appear"));
    rt_log_info(make_str("INFO - should NOT appear"));
    rt_log_warn(make_str("WARN - should NOT appear"));

    printf("--- End of suppressed output test ---\n\n");

    // Restore original level
    rt_log_set_level(original);

    printf("test_log_output: PASSED (visual inspection)\n");
}

// ============================================================================
// Default Level Tests
// ============================================================================

static void test_default_level()
{
    // Note: This test assumes the default level is INFO (1)
    // If run first before other tests modify the level
    // We can't really test this without resetting global state

    printf("test_default_level: SKIPPED (depends on global state)\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== Viper.Log Tests ===\n\n");

    // Level constants
    test_level_constants();

    // Level get/set
    test_level_get_set();

    // Enabled checks
    test_enabled();

    // Log output (visual)
    test_log_output();

    // Default level
    test_default_level();

    printf("\nAll RTLogTests passed!\n");
    return 0;
}
