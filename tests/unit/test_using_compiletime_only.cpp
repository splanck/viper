// File: tests/unit/test_using_compiletime_only.cpp
// Purpose: Verify that USING directives produce no runtime artifacts.
// Ensures USING is purely compile-time (no externs, decls, or IL ops).

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "il/io/Serializer.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

/// @brief Test that USING directives alone produce no IL artifacts.
void test_using_only_produces_no_artifacts()
{
    // First, get baseline IL size for empty program (no USING)
    std::string emptySource = "END\n";

    SourceManager smEmpty;
    DiagnosticEngine deEmpty;
    DiagnosticEmitter emitterEmpty(deEmpty, smEmpty);

    uint32_t fileIdEmpty = smEmpty.addFile("empty.bas");
    emitterEmpty.addSource(fileIdEmpty, emptySource);

    Parser parserEmpty(emptySource, fileIdEmpty);
    auto programEmpty = parserEmpty.parseProgram();

    SemanticAnalyzer analyzerEmpty(emitterEmpty);
    analyzerEmpty.analyze(*programEmpty);

    Lowerer lowererEmpty;
    lowererEmpty.setDiagnosticEmitter(&emitterEmpty);
    auto moduleEmpty = lowererEmpty.lowerProgram(*programEmpty);

    std::string ilEmpty = il::io::Serializer::toString(moduleEmpty);
    size_t baselineSize = ilEmpty.length();

    // Count baseline functions (should be 2: __mod_init and main)
    size_t baselineFuncs = 0;
    size_t pos = 0;
    while ((pos = ilEmpty.find("func ", pos)) != std::string::npos)
    {
        baselineFuncs++;
        pos += 5;
    }

    // Now test with USING directives that reference non-existent namespaces
    // (we expect E_NS_001 errors but IL should still be minimal)
    std::string sourceWithUsing = R"(
USING System
USING Collections
USING Utils.Helpers

END
)";

    SourceManager sm;
    DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);

    uint32_t fileId = sm.addFile("test.bas");
    emitter.addSource(fileId, sourceWithUsing);

    Parser parser(sourceWithUsing, fileId);
    auto program = parser.parseProgram();

    SemanticAnalyzer analyzer(emitter);
    analyzer.analyze(*program);

    // Note: Will have E_NS_001 errors for non-existent namespaces, but that's OK

    Lowerer lowerer;
    lowerer.setDiagnosticEmitter(&emitter);
    auto module = lowerer.lowerProgram(*program);

    std::string il = il::io::Serializer::toString(module);

    // Verify IL is approximately the same size as empty program
    // Allow small delta for whitespace/formatting differences
    size_t sizeDelta =
        (il.length() > baselineSize) ? (il.length() - baselineSize) : (baselineSize - il.length());

    std::cout << "  Baseline IL: " << baselineSize << " bytes, " << baselineFuncs << " functions\n";
    std::cout << "  With USING:  " << il.length() << " bytes\n";
    std::cout << "  Delta:       " << sizeDelta << " bytes\n";

    assert(sizeDelta < 200 && "USING directives should not significantly increase IL size");

    // Count functions - should be same as baseline
    size_t funcCount = 0;
    pos = 0;
    while ((pos = il.find("func ", pos)) != std::string::npos)
    {
        funcCount++;
        pos += 5;
    }

    assert(funcCount == baselineFuncs && "USING should not add function definitions");

    // Verify no additional class or type definitions beyond baseline
    bool hasClassDef = il.find("type ") != std::string::npos;
    bool hasBaselineClassDef = ilEmpty.find("type ") != std::string::npos;

    assert(hasClassDef == hasBaselineClassDef &&
           "USING should not generate class/type definitions");
}

/// @brief Test that USING with declarations produces correct artifacts.
/// This is a sanity check that USING works when combined with real code.
void test_using_with_declarations()
{
    std::string source = R"(
USING Collections

NAMESPACE Collections
  CLASS List
    DIM size AS I64
  END CLASS
END NAMESPACE

CLASS App
  DIM myList AS List
END CLASS

END
)";

    SourceManager sm;
    DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);

    uint32_t fileId = sm.addFile("test.bas");
    emitter.addSource(fileId, source);

    Parser parser(source, fileId);
    auto program = parser.parseProgram();

    SemanticAnalyzer analyzer(emitter);
    analyzer.analyze(*program);

    size_t errorCount = emitter.errorCount();
    assert(errorCount == 0 && "USING with declarations should compile without errors");

    Lowerer lowerer;
    lowerer.setDiagnosticEmitter(&emitter);
    auto module = lowerer.lowerProgram(*program);

    std::string il = il::io::Serializer::toString(module);

    // Main assertion: USING keyword should not appear in IL (it's compile-time only)
    bool hasUsingKeyword =
        il.find("USING") != std::string::npos || il.find("using") != std::string::npos;
    assert(!hasUsingKeyword && "USING keyword should not appear in IL");

    // Should have produced some IL (not minimal)
    assert(il.length() > 500 && "Program with classes should produce substantial IL");

    std::cout << "  Generated IL: " << il.length() << " bytes (with classes)\n";
}

/// @brief Test empty program (baseline for comparison).
void test_empty_program_baseline()
{
    std::string source = "END\n";

    SourceManager sm;
    DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);

    uint32_t fileId = sm.addFile("test.bas");
    emitter.addSource(fileId, source);

    Parser parser(source, fileId);
    auto program = parser.parseProgram();

    SemanticAnalyzer analyzer(emitter);
    analyzer.analyze(*program);

    Lowerer lowerer;
    lowerer.setDiagnosticEmitter(&emitter);
    auto module = lowerer.lowerProgram(*program);

    std::string il = il::io::Serializer::toString(module);

    // Baseline: empty program should produce minimal IL
    std::cout << "Empty program IL length: " << il.length() << " bytes\n";
    std::cout << "Empty program IL:\n" << il << "\n";
}

int main()
{
    std::cout << "=== USING Compile-Time Only Tests ===\n\n";

    std::cout << "Running: test_empty_program_baseline\n";
    test_empty_program_baseline();

    std::cout << "\nRunning: test_using_only_produces_no_artifacts\n";
    test_using_only_produces_no_artifacts();

    std::cout << "\nRunning: test_using_with_declarations\n";
    test_using_with_declarations();

    std::cout << "\n=== All USING compile-time tests passed ===\n";
    return 0;
}
