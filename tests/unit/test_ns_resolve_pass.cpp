// File: tests/unit/test_ns_resolve_pass.cpp
// Purpose: Test type resolution pass with TypeResolver.
// Note: Tests are simplified to work within current constraints:
//       - USING must appear before all declarations (including NAMESPACE)
//       - Cannot import namespaces defined in same file

#include "frontends/basic/SemanticAnalyzer.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "support/source_manager.hpp"
#include "support/diagnostics.hpp"
#include <cassert>
#include <string>
#include <sstream>
#include <iostream>

using namespace il::frontends::basic;
using namespace il::support;

// Helper to parse and analyze a program, returning number of errors.
size_t parseAndAnalyze(const std::string &source, DiagnosticEngine &de, bool verbose = false)
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

    if (verbose && emitter.errorCount() > 0) {
        std::cerr << "Semantic errors: " << emitter.errorCount() << std::endl;
        de.printAll(std::cerr, &sm);
    }

    return emitter.errorCount();
}

// Test: Cross-namespace base class resolution (fully qualified)
void test_cross_namespace_qualified()
{
    std::string source = R"(
100 NAMESPACE NS1
110   CLASS BaseClass
120   END CLASS
130 END NAMESPACE
140 NAMESPACE NS2
150   CLASS DerivedClass : NS1.BaseClass
160   END CLASS
170 END NAMESPACE
)";

    DiagnosticEngine de;
    size_t errorCount = parseAndAnalyze(source, de);
    // Should have no errors - valid cross-namespace inheritance with FQ name.
    assert(errorCount == 0);
}

// Test: Valid resolution in same namespace
void test_same_namespace_resolution()
{
    std::string source = R"(
100 NAMESPACE MyNS
110   CLASS BaseClass
120   END CLASS
130   CLASS DerivedClass : BaseClass
140   END CLASS
150 END NAMESPACE
)";

    DiagnosticEngine de;
    size_t errorCount = parseAndAnalyze(source, de);
    // Should have no errors - base class in same namespace.
    assert(errorCount == 0);
}

// Test: Type not found (E_NS_006 or old B2101)
void test_type_not_found()
{
    std::string source = R"(
100 CLASS MyClass : NonExistentType
110 END CLASS
)";

    DiagnosticEngine de;
    size_t errorCount = parseAndAnalyze(source, de);
    assert(errorCount > 0);
}

// Test: Nested namespace with fully qualified name
void test_nested_namespace()
{
    std::string source = R"(
100 NAMESPACE Outer.Inner
110   CLASS BaseClass
120   END CLASS
130 END NAMESPACE
140 CLASS DerivedClass : Outer.Inner.BaseClass
150 END CLASS
)";

    DiagnosticEngine de;
    size_t errorCount = parseAndAnalyze(source, de);
    // Should have no errors - valid nested namespace reference.
    assert(errorCount == 0);
}

int main()
{
    test_cross_namespace_qualified();
    test_same_namespace_resolution();
    test_type_not_found();
    test_nested_namespace();
    return 0;
}
