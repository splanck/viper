//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file TestHarness.hpp
/// @brief Minimal, dependency-free unit testing framework for the Viper project.
///
/// @details This header provides a lightweight test harness that enables writing
/// and running unit tests without external dependencies like Google Test or Catch2.
/// The framework is intentionally simple to facilitate rapid compilation and
/// minimize build complexity.
///
/// ## Architecture Overview
///
/// The test harness consists of three main components:
///
/// ### 1. Test Registry
///
/// Tests are registered at static initialization time using the `TEST()` macro.
/// Each test case is stored in a global registry (implemented as a Meyer's
/// singleton vector) containing the suite name, test name, and test function:
///
/// ```cpp
/// TEST(MySuite, MyTest) {
///     EXPECT_EQ(1 + 1, 2);
/// }
/// ```
///
/// ### 2. Assertion Macros
///
/// Two families of assertion macros are provided:
///
/// | Macro Family | Behavior on Failure |
/// |--------------|---------------------|
/// | `EXPECT_*`   | Reports failure but continues test execution |
/// | `ASSERT_*`   | Reports failure and immediately aborts the test |
///
/// Available assertions:
/// - `EXPECT_TRUE(expr)` / `ASSERT_TRUE(expr)` - Verifies expression is truthy
/// - `EXPECT_FALSE(expr)` / `ASSERT_FALSE(expr)` - Verifies expression is falsy
/// - `EXPECT_EQ(a, b)` / `ASSERT_EQ(a, b)` - Verifies equality
/// - `EXPECT_NE(a, b)` / `ASSERT_NE(a, b)` - Verifies inequality
///
/// ### 3. Exception-Based Control Flow
///
/// Assertion failures and test skips are communicated via exceptions:
/// - `TestFailure` - Thrown on assertion failure (fatal flag controls abort)
/// - `TestSkip` - Thrown when `VIPER_TEST_SKIP()` is called
///
/// ## Usage Example
///
/// ```cpp
/// #include "tests/TestHarness.hpp"
///
/// TEST(MathSuite, Addition) {
///     int result = 2 + 2;
///     ASSERT_EQ(result, 4);
///     EXPECT_TRUE(result > 0);
/// }
///
/// TEST(MathSuite, Division) {
///     if (!hasFpuSupport())
///         VIPER_TEST_SKIP("FPU not available");
///     EXPECT_EQ(10.0 / 2.0, 5.0);
/// }
///
/// int main(int argc, char** argv) {
///     viper_test::init(&argc, argv);
///     return viper_test::run_all_tests();
/// }
/// ```
///
/// ## Output Format
///
/// Test results are printed to stdout/stderr in a format similar to Google Test:
///
/// ```
/// [  PASSED  ] MathSuite.Addition
/// [ SKIPPED  ] MathSuite.Division: FPU not available
/// [  FAILED  ] StringSuite.Parse
/// 1 test(s) failed.
/// 1 test(s) skipped.
/// ```
///
/// ## Design Decisions
///
/// - **No external dependencies**: Compiles with just the standard library
/// - **Header-only**: No separate compilation unit required
/// - **Exception-based**: Uses C++ exceptions for control flow (requires RTTI)
/// - **Static registration**: Tests auto-register before main() runs
/// - **Compatible API**: Macro names mirror Google Test for familiarity
///
/// @see viper_test::run_all_tests() - Entry point for test execution
/// @see viper_test::TestCase - Test case descriptor structure
///
//===----------------------------------------------------------------------===//

#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace viper_test
{

/// @brief Exception thrown when a test assertion fails.
///
/// @details This exception is caught by the test runner to determine whether
/// to continue executing the current test (non-fatal) or abort immediately
/// (fatal). The `fatal` flag distinguishes EXPECT failures (continue) from
/// ASSERT failures (abort).
///
/// ## Example
///
/// ```cpp
/// // EXPECT_TRUE throws TestFailure with fatal=false
/// // ASSERT_TRUE throws TestFailure with fatal=true
/// ```
///
/// @invariant Once thrown, the test runner will record the failure.
struct TestFailure final : public std::exception
{
    /// @brief Construct a test failure exception.
    /// @param fatal If true, the test runner will stop executing the current test.
    explicit TestFailure(bool fatal) : fatal(fatal) {}

    /// @brief Whether this failure should abort the current test.
    bool fatal = false;

    /// @brief Return a human-readable description of the failure.
    /// @return "fatal test failure" if fatal, otherwise "test failure".
    const char *what() const noexcept override
    {
        return fatal ? "fatal test failure" : "test failure";
    }
};

/// @brief Exception thrown to skip a test with an explanatory message.
///
/// @details Tests can call `VIPER_TEST_SKIP("reason")` to indicate they
/// cannot run in the current environment (e.g., missing hardware, unsupported
/// platform). The test runner catches this exception and marks the test as
/// skipped rather than failed.
///
/// ## Example
///
/// ```cpp
/// TEST(Hardware, GpuCompute) {
///     if (!hasGpu())
///         VIPER_TEST_SKIP("No GPU available");
///     // ... GPU tests ...
/// }
/// ```
struct TestSkip final : public std::exception
{
    /// @brief Construct a skip exception with an explanatory reason.
    /// @param reason Human-readable explanation for why the test was skipped.
    explicit TestSkip(std::string reason) : reason(std::move(reason)) {}

    /// @brief The reason this test was skipped.
    std::string reason;

    /// @brief Return the skip reason.
    /// @return Pointer to the reason string.
    const char *what() const noexcept override
    {
        return reason.c_str();
    }
};

/// @brief Descriptor for a single test case in the registry.
///
/// @details Each test registered via the `TEST()` macro creates a TestCase
/// containing the suite name (for grouping), test name (for identification),
/// and a callable that executes the test body.
struct TestCase
{
    std::string suite;        ///< Name of the test suite (first TEST argument).
    std::string name;         ///< Name of the individual test (second TEST argument).
    std::function<void()> fn; ///< The test function body to execute.
};

/// @brief Access the global test registry.
///
/// @details Returns a reference to the singleton vector that stores all
/// registered test cases. Tests are added to this registry at static
/// initialization time before main() runs. The registry uses Meyer's
/// singleton pattern to ensure proper initialization order.
///
/// @return Reference to the global test case vector.
inline std::vector<TestCase> &registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

/// @brief Helper struct that registers a test case at static initialization.
///
/// @details The `TEST()` macro creates a static instance of this struct,
/// causing the test to be added to the global registry before main() runs.
/// This pattern enables automatic test discovery without explicit registration.
struct TestRegistrar
{
    /// @brief Register a test case in the global registry.
    ///
    /// @param suiteName Name of the test suite for grouping.
    /// @param testName Name of the individual test.
    /// @param fn Callable that executes the test body.
    TestRegistrar(std::string suiteName, std::string testName, std::function<void()> fn)
    {
        registry().push_back({std::move(suiteName), std::move(testName), std::move(fn)});
    }
};

/// @brief Initialize the test framework (no-op in this implementation).
///
/// @details Provided for API compatibility with frameworks that require
/// command-line argument processing. This implementation ignores arguments.
///
/// @param argc Pointer to argument count (unused).
/// @param argv Pointer to argument array (unused).
inline void init(int *, char ***) {}

/// @brief Initialize the test framework (no-op in this implementation).
///
/// @details Overload that accepts argv directly rather than as a pointer.
/// Provided for flexibility in how main() passes arguments.
///
/// @param argc Pointer to argument count (unused).
/// @param argv Argument array (unused).
inline void init(int *, char **) {}

/// @brief Report an assertion failure and throw a TestFailure exception.
///
/// @details Called by EXPECT_* and ASSERT_* macros when an assertion fails.
/// Prints the failed expression, source location, and then throws an exception
/// to signal the failure to the test runner.
///
/// @param expr String representation of the failed expression (from macro).
/// @param file Source file where the failure occurred (__FILE__).
/// @param line Line number where the failure occurred (__LINE__).
/// @param fatal If true, throw a fatal failure that aborts the test.
inline void report_failure(std::string_view expr, const char *file, int line, bool fatal)
{
    std::cerr << file << ":" << line << ": failure\n";
    std::cerr << "  expected: " << expr << "\n";
    throw TestFailure(fatal);
}

/// @brief Skip the current test with an explanatory reason.
///
/// @details Called by the VIPER_TEST_SKIP() macro to indicate that a test
/// cannot run in the current environment. This function never returns;
/// it always throws a TestSkip exception.
///
/// @param reason Human-readable explanation for why the test is being skipped.
[[noreturn]] inline void skip(std::string reason)
{
    throw TestSkip(std::move(reason));
}

/// @brief Execute all registered tests and return the failure count.
///
/// @details Iterates through every test case in the global registry, executing
/// each test function and catching any exceptions to determine the outcome:
///
/// | Exception Type     | Outcome         | Continues? |
/// |--------------------|-----------------|------------|
/// | None               | PASSED          | Yes        |
/// | TestSkip           | SKIPPED         | Yes        |
/// | TestFailure(false) | FAILED          | Yes        |
/// | TestFailure(true)  | FAILED (abort)  | No         |
/// | std::exception     | FAILED          | Yes        |
/// | Unknown exception  | FAILED          | Yes        |
///
/// ## Output Format
///
/// Results are printed in a format compatible with CI systems:
/// ```
/// [  PASSED  ] Suite.Test
/// [ SKIPPED  ] Suite.Test: reason
/// [  FAILED  ] Suite.Test
/// ```
///
/// ## Usage
///
/// ```cpp
/// int main(int argc, char** argv) {
///     viper_test::init(&argc, argv);
///     return viper_test::run_all_tests();
/// }
/// ```
///
/// @return Number of failed tests (0 indicates all tests passed or were skipped).
inline int run_all_tests()
{
    int failures = 0;
    int skips = 0;
    for (const auto &t : registry())
    {
        try
        {
            t.fn();
            std::cout << "[  PASSED  ] " << t.suite << "." << t.name << "\n";
        }
        catch (const TestSkip &s)
        {
            ++skips;
            std::cout << "[ SKIPPED  ] " << t.suite << "." << t.name << ": " << s.what() << "\n";
        }
        catch (const TestFailure &f)
        {
            ++failures;
            std::cerr << "[  FAILED  ] " << t.suite << "." << t.name;
            if (!f.fatal)
                std::cerr << " (non-fatal)";
            std::cerr << "\n";
            if (f.fatal)
            {
                std::cerr << "Stopping due to ASSERT failure.\n";
                break;
            }
        }
        catch (const std::exception &e)
        {
            ++failures;
            std::cerr << "[  FAILED  ] " << t.suite << "." << t.name
                      << " (unhandled exception: " << e.what() << ")\n";
        }
        catch (...)
        {
            ++failures;
            std::cerr << "[  FAILED  ] " << t.suite << "." << t.name << " (unknown exception)\n";
        }
    }
    if (failures != 0)
        std::cerr << failures << " test(s) failed.\n";
    if (skips != 0)
        std::cout << skips << " test(s) skipped.\n";
    return failures;
}

} // namespace viper_test

//===----------------------------------------------------------------------===//
// Implementation Detail Macros
//===----------------------------------------------------------------------===//

/// @cond INTERNAL
/// Internal macro for token concatenation (indirection required for expansion).
#define VIPER_TEST_DETAIL_CAT_INNER(a, b) a##b
/// Internal macro for token concatenation.
#define VIPER_TEST_DETAIL_CAT(a, b) VIPER_TEST_DETAIL_CAT_INNER(a, b)
/// @endcond

//===----------------------------------------------------------------------===//
// Test Definition Macro
//===----------------------------------------------------------------------===//

/// @def TEST(SuiteName, TestName)
/// @brief Define and register a test case.
///
/// @details Creates a function that will be called when `run_all_tests()` is
/// invoked. The test is automatically registered in the global registry at
/// static initialization time, before main() runs.
///
/// ## Example
///
/// ```cpp
/// TEST(MySuite, MyTest) {
///     int x = compute();
///     EXPECT_EQ(x, 42);
/// }
/// ```
///
/// @param SuiteName Identifier for the test suite (used for grouping).
/// @param TestName Identifier for this specific test.
#define TEST(SuiteName, TestName)                                                                  \
    static void VIPER_TEST_DETAIL_CAT(SuiteName, TestName)();                                      \
    static const ::viper_test::TestRegistrar VIPER_TEST_DETAIL_CAT(SuiteName,                      \
                                                                   TestName##Registrar)(           \
        #SuiteName, #TestName, VIPER_TEST_DETAIL_CAT(SuiteName, TestName));                        \
    static void VIPER_TEST_DETAIL_CAT(SuiteName, TestName)()

//===----------------------------------------------------------------------===//
// Expectation Macros (Non-Fatal Assertions)
//===----------------------------------------------------------------------===//

/// @def EXPECT_TRUE(expr)
/// @brief Assert that an expression evaluates to true (non-fatal).
///
/// @details If the expression is false, reports a failure but continues
/// executing the test. Use when you want to check multiple conditions
/// even if earlier ones fail.
///
/// @param expr Expression to evaluate.
#define EXPECT_TRUE(expr)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expr))                                                                               \
            ::viper_test::report_failure(#expr, __FILE__, __LINE__, false);                        \
    } while (false)

/// @def EXPECT_FALSE(expr)
/// @brief Assert that an expression evaluates to false (non-fatal).
///
/// @param expr Expression to evaluate (should be false for the test to pass).
#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))

/// @def EXPECT_EQ(val1, val2)
/// @brief Assert that two values are equal using operator== (non-fatal).
///
/// @param val1 First value to compare.
/// @param val2 Second value to compare.
#define EXPECT_EQ(val1, val2)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if (!((val1) == (val2)))                                                                   \
            ::viper_test::report_failure(#val1 " == " #val2, __FILE__, __LINE__, false);           \
    } while (false)

/// @def EXPECT_NE(val1, val2)
/// @brief Assert that two values are not equal using operator!= (non-fatal).
///
/// @param val1 First value to compare.
/// @param val2 Second value to compare.
#define EXPECT_NE(val1, val2)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if (!((val1) != (val2)))                                                                   \
            ::viper_test::report_failure(#val1 " != " #val2, __FILE__, __LINE__, false);           \
    } while (false)

//===----------------------------------------------------------------------===//
// Assertion Macros (Fatal Assertions)
//===----------------------------------------------------------------------===//

/// @def ASSERT_TRUE(expr)
/// @brief Assert that an expression evaluates to true (fatal).
///
/// @details If the expression is false, reports a failure and immediately
/// aborts the current test. Use when subsequent test code depends on this
/// condition being true.
///
/// @param expr Expression to evaluate.
#define ASSERT_TRUE(expr)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expr))                                                                               \
            ::viper_test::report_failure(#expr, __FILE__, __LINE__, true);                         \
    } while (false)

/// @def ASSERT_FALSE(expr)
/// @brief Assert that an expression evaluates to false (fatal).
///
/// @param expr Expression to evaluate (should be false for the test to pass).
#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

/// @def ASSERT_EQ(val1, val2)
/// @brief Assert that two values are equal using operator== (fatal).
///
/// @param val1 First value to compare.
/// @param val2 Second value to compare.
#define ASSERT_EQ(val1, val2)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if (!((val1) == (val2)))                                                                   \
            ::viper_test::report_failure(#val1 " == " #val2, __FILE__, __LINE__, true);            \
    } while (false)

/// @def ASSERT_NE(val1, val2)
/// @brief Assert that two values are not equal using operator!= (fatal).
///
/// @param val1 First value to compare.
/// @param val2 Second value to compare.
#define ASSERT_NE(val1, val2)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if (!((val1) != (val2)))                                                                   \
            ::viper_test::report_failure(#val1 " != " #val2, __FILE__, __LINE__, true);            \
    } while (false)

//===----------------------------------------------------------------------===//
// Test Skip Macro
//===----------------------------------------------------------------------===//

/// @def VIPER_TEST_SKIP(reason)
/// @brief Skip the current test with an explanatory reason.
///
/// @details Use this macro when a test cannot run in the current environment
/// (e.g., missing hardware, unsupported platform, or unmet preconditions).
/// The test will be marked as skipped rather than failed.
///
/// ## Example
///
/// ```cpp
/// TEST(Network, WebSocket) {
///     if (!networkAvailable())
///         VIPER_TEST_SKIP("No network connection");
///     // ... network tests ...
/// }
/// ```
///
/// @param reason Human-readable string explaining why the test is skipped.
#define VIPER_TEST_SKIP(reason)                                                                    \
    do                                                                                             \
    {                                                                                              \
        ::viper_test::skip(reason);                                                                \
    } while (false)
