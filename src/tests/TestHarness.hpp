//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/TestHarness.hpp
// Purpose: Minimal unit-test harness for the Viper project.
//
// This header provides a tiny test registry plus a small set of ASSERT/EXPECT
// macros used across the codebase. It is intentionally dependency-free.
//
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

struct TestFailure final : public std::exception
{
    explicit TestFailure(bool fatal) : fatal(fatal) {}

    bool fatal = false;

    const char *what() const noexcept override
    {
        return fatal ? "fatal test failure" : "test failure";
    }
};

struct TestSkip final : public std::exception
{
    explicit TestSkip(std::string reason) : reason(std::move(reason)) {}

    std::string reason;

    const char *what() const noexcept override
    {
        return reason.c_str();
    }
};

struct TestCase
{
    std::string suite;
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase> &registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

struct TestRegistrar
{
    TestRegistrar(std::string suiteName, std::string testName, std::function<void()> fn)
    {
        registry().push_back({std::move(suiteName), std::move(testName), std::move(fn)});
    }
};

inline void init(int *, char ***) {}

inline void init(int *, char **) {}

inline void report_failure(std::string_view expr, const char *file, int line, bool fatal)
{
    std::cerr << file << ":" << line << ": failure\n";
    std::cerr << "  expected: " << expr << "\n";
    throw TestFailure(fatal);
}

[[noreturn]] inline void skip(std::string reason)
{
    throw TestSkip(std::move(reason));
}

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

#define VIPER_TEST_DETAIL_CAT_INNER(a, b) a##b
#define VIPER_TEST_DETAIL_CAT(a, b) VIPER_TEST_DETAIL_CAT_INNER(a, b)

#define TEST(SuiteName, TestName)                                                                  \
    static void VIPER_TEST_DETAIL_CAT(SuiteName, TestName)();                                      \
    static const ::viper_test::TestRegistrar VIPER_TEST_DETAIL_CAT(SuiteName,                      \
                                                                   TestName##Registrar)(           \
        #SuiteName, #TestName, VIPER_TEST_DETAIL_CAT(SuiteName, TestName));                        \
    static void VIPER_TEST_DETAIL_CAT(SuiteName, TestName)()

#define EXPECT_TRUE(expr)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expr))                                                                               \
            ::viper_test::report_failure(#expr, __FILE__, __LINE__, false);                        \
    } while (false)

#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))

#define ASSERT_TRUE(expr)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expr))                                                                               \
            ::viper_test::report_failure(#expr, __FILE__, __LINE__, true);                         \
    } while (false)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define EXPECT_EQ(val1, val2)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if (!((val1) == (val2)))                                                                   \
            ::viper_test::report_failure(#val1 " == " #val2, __FILE__, __LINE__, false);           \
    } while (false)

#define EXPECT_NE(val1, val2)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if (!((val1) != (val2)))                                                                   \
            ::viper_test::report_failure(#val1 " != " #val2, __FILE__, __LINE__, false);           \
    } while (false)

#define ASSERT_EQ(val1, val2)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if (!((val1) == (val2)))                                                                   \
            ::viper_test::report_failure(#val1 " == " #val2, __FILE__, __LINE__, true);            \
    } while (false)

#define ASSERT_NE(val1, val2)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if (!((val1) != (val2)))                                                                   \
            ::viper_test::report_failure(#val1 " != " #val2, __FILE__, __LINE__, true);            \
    } while (false)

#define VIPER_TEST_SKIP(reason)                                                                    \
    do                                                                                             \
    {                                                                                              \
        ::viper_test::skip(reason);                                                                \
    } while (false)
