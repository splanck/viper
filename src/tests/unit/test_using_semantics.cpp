//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_using_semantics.cpp
// Purpose: Test semantic validation of USING directives. 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

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

// Helper to parse and analyze a program, returning number of errors.
size_t parseAndAnalyze(const std::string &source, DiagnosticEngine &de)
{
    SourceManager sm;
    uint32_t fileId = sm.addFile("test.bas");

    // Parse the program.
    Parser parser(source, fileId);
    auto program = parser.parseProgram();

    // Analyze with diagnostics.
    DiagnosticEmitter emitter(de, sm);
    emitter.addSource(fileId, source);
    SemanticAnalyzer analyzer(emitter);
    analyzer.analyze(*program);

    return emitter.errorCount();
}

// Test: USING inside namespace (Phase 2 allows scoped USING inside namespace)
void test_using_inside_namespace()
{
    // In Phase 2 we allow USING inside namespace blocks.
    // Use an existing namespace to avoid unknown-namespace diagnostics.
    std::string source2 = R"(
NAMESPACE A
END NAMESPACE
NAMESPACE B
  USING A
END NAMESPACE
)";
    DiagnosticEngine de2;
    size_t err2 = parseAndAnalyze(source2, de2);
    // Phase 2: scoped USING inside namespace is allowed (no error expected).
    assert(err2 == 0);
}

// Test: USING after first decl (Phase 2 allows file-scoped USING anywhere)
void test_using_after_decl()
{
    std::string source = R"(
NAMESPACE A
END NAMESPACE
USING A
)";

    DiagnosticEngine de;
    size_t errorCount = parseAndAnalyze(source, de);
    // Spec: USING must appear before declarations (E_NS_005)
    assert(errorCount > 0);
}

// Test: USING after class decl (Phase 2 allows file-scoped USING anywhere)
void test_using_after_class()
{
    std::string source = R"(
CLASS MyClass
END CLASS
NAMESPACE A
END NAMESPACE
USING A
)";

    DiagnosticEngine de;
    size_t errorCount = parseAndAnalyze(source, de);
    // Spec: USING must appear before declarations (E_NS_005)
    assert(errorCount > 0);
}

// Test: USING NonExistent.Namespace → E_NS_001
void test_using_nonexistent_namespace()
{
    std::string source = R"(
USING NonExistent.Namespace
)";

    DiagnosticEngine de;
    size_t errorCount = parseAndAnalyze(source, de);
    assert(errorCount > 0);
}

// Test: USING X = A then USING X = B → E_NS_004
void test_duplicate_alias()
{
    std::string source = R"(
NAMESPACE A
END NAMESPACE
NAMESPACE B
END NAMESPACE
USING X = A
USING X = B
)";

    DiagnosticEngine de;
    size_t errorCount = parseAndAnalyze(source, de);
    assert(errorCount > 0);
}

// Test: USING alias that equals a namespace name (e.g., "A") → E_NS_007
void test_alias_shadows_namespace()
{
    std::string source = R"(
NAMESPACE A
END NAMESPACE
NAMESPACE B
END NAMESPACE
USING A = B
)";

    DiagnosticEngine de;
    size_t errorCount = parseAndAnalyze(source, de);
    assert(errorCount > 0);
}

// Test: NAMESPACE Viper … → E_NS_009
void test_reserved_viper_namespace()
{
    std::string source = R"(
NAMESPACE Viper
END NAMESPACE
)";

    DiagnosticEngine de;
    size_t errorCount = parseAndAnalyze(source, de);
    assert(errorCount > 0);
}

// Test: USING Viper … → E_NS_009
void test_reserved_viper_using()
{
    std::string source = R"(
NAMESPACE Viper
END NAMESPACE
USING Viper
)";

    DiagnosticEngine de;
    size_t errorCount = parseAndAnalyze(source, de);
    assert(errorCount > 0);
}

// Test: Valid USING at file scope before declarations
void test_valid_using()
{
    std::string source = R"(
100 USING System
110 NAMESPACE System
120 END NAMESPACE
130 NAMESPACE MyApp
140 END NAMESPACE
)";

    DiagnosticEngine de;
    size_t errorCount = parseAndAnalyze(source, de);
    // Should have no errors - valid USING placement.
    assert(errorCount == 0);
}

// Test: Valid USING with alias
void test_valid_using_with_alias()
{
    std::string source = R"(
100 USING SC = System.Collections
110 NAMESPACE System.Collections
120 END NAMESPACE
130 NAMESPACE MyApp
140 END NAMESPACE
)";

    DiagnosticEngine de;
    size_t errorCount = parseAndAnalyze(source, de);
    // Should have no errors - valid USING with alias.
    assert(errorCount == 0);
}

// Test: Case-insensitive Viper check
void test_reserved_viper_case_insensitive()
{
    std::string source = R"(
NAMESPACE viper
END NAMESPACE
)";

    DiagnosticEngine de;
    size_t errorCount = parseAndAnalyze(source, de);
    assert(errorCount > 0);
}

int main()
{
    test_using_inside_namespace();
    test_using_after_decl();
    test_using_after_class();
    test_using_nonexistent_namespace();
    test_duplicate_alias();
    test_alias_shadows_namespace();
    test_reserved_viper_namespace();
    test_reserved_viper_using();
    test_valid_using();
    test_valid_using_with_alias();
    test_reserved_viper_case_insensitive();
    return 0;
}
