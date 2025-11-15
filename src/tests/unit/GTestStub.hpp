// File: tests/unit/GTestStub.hpp
// Purpose: Minimal GoogleTest-compatible shim for environments without gtest.
// Key invariants: Provides EXPECT_TRUE/ASSERT_TRUE/EXPECT_NE macros and TEST registration.
// Ownership/Lifetime: Static registry persists for program lifetime.
// Links: https://github.com/google/googletest (reference for API shape)

#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace testing
{
struct TestFailure : public std::exception
{
    explicit TestFailure(bool fatal) : fatal(fatal) {}

    bool fatal;
};

struct TestCase
{
    std::string suite;
    std::string name;
    std::function<void()> func;
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

inline void InitGoogleTest(int *, char ***) {}

inline void InitGoogleTest(int *, char **) {}

inline int RUN_ALL_TESTS()
{
    int failures = 0;
    for (const auto &test : registry())
    {
        try
        {
            test.func();
            std::cout << "[  PASSED  ] " << test.suite << "." << test.name << "\n";
        }
        catch (const TestFailure &failure)
        {
            ++failures;
            std::cerr << "[  FAILED  ] " << test.suite << "." << test.name;
            if (!failure.fatal)
            {
                std::cerr << " (non-fatal)";
            }
            std::cerr << "\n";
            if (failure.fatal)
            {
                std::cerr << "Stopping due to ASSERT failure.\n";
                break;
            }
        }
    }
    if (failures != 0)
    {
        std::cerr << failures << " test(s) failed." << std::endl;
    }
    return failures;
}

inline void reportFailure(std::string_view expr, const char *file, int line, bool fatal)
{
    std::cerr << file << ":" << line << ": Failure\n";
    std::cerr << "  Expected: " << expr << "\n";
    throw TestFailure(fatal);
}

} // namespace testing

#define GTEST_DETAIL_CAT_INNER(a, b) a##b
#define GTEST_DETAIL_CAT(a, b) GTEST_DETAIL_CAT_INNER(a, b)

#define TEST(SuiteName, TestName)                                                                  \
    static void GTEST_DETAIL_CAT(SuiteName, TestName)();                                           \
    static const ::testing::TestRegistrar GTEST_DETAIL_CAT(SuiteName, TestName##Registrar)(        \
        #SuiteName, #TestName, GTEST_DETAIL_CAT(SuiteName, TestName));                             \
    static void GTEST_DETAIL_CAT(SuiteName, TestName)()

#define EXPECT_TRUE(expr)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expr))                                                                               \
        {                                                                                          \
            ::testing::reportFailure(#expr, __FILE__, __LINE__, false);                            \
        }                                                                                          \
    } while (false)

#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))

#define ASSERT_TRUE(expr)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expr))                                                                               \
        {                                                                                          \
            ::testing::reportFailure(#expr, __FILE__, __LINE__, true);                             \
        }                                                                                          \
    } while (false)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define EXPECT_EQ(val1, val2)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if (!((val1) == (val2)))                                                                   \
        {                                                                                          \
            ::testing::reportFailure(#val1 " == " #val2, __FILE__, __LINE__, false);               \
        }                                                                                          \
    } while (false)

#define EXPECT_NE(val1, val2)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if (!((val1) != (val2)))                                                                   \
        {                                                                                          \
            ::testing::reportFailure(#val1 " != " #val2, __FILE__, __LINE__, false);               \
        }                                                                                          \
    } while (false)

#define ASSERT_EQ(val1, val2)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if (!((val1) == (val2)))                                                                   \
        {                                                                                          \
            ::testing::reportFailure(#val1 " == " #val2, __FILE__, __LINE__, true);                \
        }                                                                                          \
    } while (false)

#define ASSERT_NE(val1, val2)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if (!((val1) != (val2)))                                                                   \
        {                                                                                          \
            ::testing::reportFailure(#val1 " != " #val2, __FILE__, __LINE__, true);                \
        }                                                                                          \
    } while (false)

#define RUN_ALL_TESTS() ::testing::RUN_ALL_TESTS()
