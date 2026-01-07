//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_for_loop_ctrl_diagnostics.cpp
// Purpose: Verify diagnostics for unsupported FOR loop control variable forms.
// Key invariants: Array elements and arbitrary expressions as FOR control
//                 variables emit compile-time errors.
// Ownership/Lifetime: Test owns compiler inputs and source manager.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicCompiler.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <sstream>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

/// @brief Compile source and return the diagnostic output.
static std::string compileToDiagnostics(const std::string &source)
{
    SourceManager sm;
    BasicCompilerOptions options{};
    BasicCompilerInput input{source, "test.bas"};
    auto result = compileBasic(input, options, sm);

    std::ostringstream oss;
    result.emitter->printAll(oss);
    return oss.str();
}

/// @brief Test that FOR loop with array element control variable emits error.
static void test_for_array_element_ctrl()
{
    // FOR arr(i) = 1 TO 10 should produce an error since array element
    // control variables are not yet supported.
    const std::string src = "DIM arr(10) AS INTEGER\n"
                            "DIM i AS INTEGER\n"
                            "i = 0\n"
                            "FOR arr(i) = 1 TO 10\n"
                            "    PRINT arr(i)\n"
                            "NEXT\n";

    std::string diag = compileToDiagnostics(src);

    // Debug output on failure
    if (diag.find("FOR control variable") == std::string::npos)
    {
        fprintf(stderr, "test_for_array_element_ctrl - Actual output:\n%s\n", diag.c_str());
    }

    // Check for the expected error message
    assert(diag.find("E_FOR_ARRAY_CTRL") != std::string::npos);
    assert(diag.find("FOR control variable") != std::string::npos);
    assert(diag.find("array element") != std::string::npos);
    assert(diag.find("ARR") != std::string::npos);
}

/// @brief Test that FOR loop with 2D array element control variable emits error.
static void test_for_2d_array_element_ctrl()
{
    // FOR matrix(i, j) = 1 TO 10 should produce the same error.
    const std::string src = "DIM matrix(5, 5) AS INTEGER\n"
                            "DIM i AS INTEGER\n"
                            "DIM j AS INTEGER\n"
                            "i = 0\n"
                            "j = 0\n"
                            "FOR matrix(i, j) = 1 TO 10\n"
                            "    PRINT matrix(i, j)\n"
                            "NEXT\n";

    std::string diag = compileToDiagnostics(src);

    // Debug output on failure
    if (diag.find("FOR control variable") == std::string::npos)
    {
        fprintf(stderr, "test_for_2d_array_element_ctrl - Actual output:\n%s\n", diag.c_str());
    }

    // Check for the expected error message
    assert(diag.find("E_FOR_ARRAY_CTRL") != std::string::npos);
    assert(diag.find("FOR control variable") != std::string::npos);
    assert(diag.find("array element") != std::string::npos);
    assert(diag.find("MATRIX") != std::string::npos);
}

/// @brief Test that normal FOR loop with simple variable still works.
static void test_for_simple_variable_works()
{
    // FOR i = 1 TO 5 should compile without errors.
    const std::string src = "DIM i AS INTEGER\n"
                            "FOR i = 1 TO 5\n"
                            "    PRINT i\n"
                            "NEXT\n";

    SourceManager sm;
    BasicCompilerOptions options{};
    BasicCompilerInput input{src, "test.bas"};
    auto result = compileBasic(input, options, sm);

    // Should succeed with no errors
    assert(result.succeeded());
    assert(result.emitter->errorCount() == 0);
}

/// @brief Test that FOR loop with object field control variable still works.
static void test_for_member_field_works()
{
    // FOR obj.field = 1 TO 5 should compile without errors.
    const std::string src = "CLASS Counter\n"
                            "    PUBLIC value AS INTEGER\n"
                            "    SUB New()\n"
                            "        value = 0\n"
                            "    END SUB\n"
                            "END CLASS\n"
                            "\n"
                            "DIM c AS Counter\n"
                            "c = NEW Counter()\n"
                            "FOR c.value = 1 TO 5\n"
                            "    PRINT c.value\n"
                            "NEXT\n";

    SourceManager sm;
    BasicCompilerOptions options{};
    BasicCompilerInput input{src, "test.bas"};
    auto result = compileBasic(input, options, sm);

    // Debug output on failure
    if (!result.succeeded())
    {
        std::ostringstream oss;
        result.emitter->printAll(oss);
        fprintf(
            stderr, "test_for_member_field_works - Unexpected errors:\n%s\n", oss.str().c_str());
    }

    // Should succeed with no errors
    assert(result.succeeded());
    assert(result.emitter->errorCount() == 0);
}

int main()
{
    test_for_array_element_ctrl();
    test_for_2d_array_element_ctrl();
    test_for_simple_variable_works();
    test_for_member_field_works();
    return 0;
}
