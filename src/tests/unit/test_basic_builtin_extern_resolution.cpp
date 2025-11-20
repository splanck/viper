// File: tests/unit/test_basic_builtin_extern_resolution.cpp
// Purpose: Validate resolution of builtin extern calls (dotted and via USING),
//          and report an error when user code shadows a builtin extern.

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <sstream>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

static std::string analyzeAndGetOutput(const std::string &source)
{
    SourceManager sm;
    uint32_t fileId = sm.addFile("test.bas");
    Parser parser(source, fileId);
    auto program = parser.parseProgram();
    DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);
    emitter.addSource(fileId, source);
    SemanticAnalyzer analyzer(emitter);
    analyzer.analyze(*program);
    std::ostringstream oss;
    de.printAll(oss, &sm);
    return oss.str();
}

static void test_direct_qualified_builtin_call()
{
    // Fully-qualified dotted call to builtin extern.
    // Accept either zero diagnostics or parser choosing a different path that still compiles.
    const std::string src = R"(
100 Viper.Console.PrintI64(42)
)";
    std::string out = analyzeAndGetOutput(src);
    // Should not report unknown procedure for the canonical qualified name.
    assert(out.find("unknown procedure 'viper.console.printi64'") == std::string::npos);
}

static void test_using_import_then_unqualified_call()
{
    // Import the Console namespace, then call PrintI64 unqualified.
    const std::string src = R"(
10 USING Viper.Console
20 PrintI64(42)
)";
    std::string out = analyzeAndGetOutput(src);
    // Should not report unknown procedure for PrintI64; accept zero diagnostics.
    assert(out.find("unknown procedure") == std::string::npos);
}

static void test_shadowing_builtin_extern()
{
    // Attempt to declare a user SUB that collides with a builtin extern.
    const std::string src = R"(
100 SUB Viper.Console.PrintI64(x AS INTEGER)
110 END SUB
)";
    std::string out = analyzeAndGetOutput(src);
    // Expect the dedicated shadowing diagnostic code to appear.
    assert(out.find("E_VIPER_BUILTIN_SHADOW") != std::string::npos ||
           out.find("shadows builtin extern") != std::string::npos);
}

int main()
{
    test_direct_qualified_builtin_call();
    test_using_import_then_unqualified_call();
    test_shadowing_builtin_extern();
    return 0;
}
