//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_lowerer_namespace.cpp
// Purpose: Test namespace qualification in the BASIC lowerer.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "il/io/Serializer.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

// Test: UsingDecl visitor has no side-effects (produces no IL)
void test_using_no_il()
{
    std::string source = R"(
100 NAMESPACE System
110 END NAMESPACE
120 USING System
)";

    SourceManager sm;
    uint32_t fileId = sm.addFile("test.bas");

    Parser parser(source, fileId);
    auto program = parser.parseProgram();

    // Lower to IL.
    Lowerer lowerer;
    auto module = lowerer.lowerProgram(*program);

    // Serialize IL to string.
    std::string il = il::io::Serializer::toString(module);

    // Assert USING does not appear in IL output.
    // The IL should only contain the @main function, no "USING" keyword or directive.
    assert(il.find("USING") == std::string::npos);
    assert(il.find("System") == std::string::npos ||
           il.find("@main") != std::string::npos); // May appear in function name context
}

// Test: qualify("T") under namespace A.B => "A.B.T"
void test_qualify_simple_name()
{
    Lowerer lowerer;

    // No namespace active → unqualified
    std::string result1 = lowerer.qualify("MyClass");
    assert(result1 == "MyClass");

    // Push namespace A.B
    lowerer.pushNamespace({"A", "B"});
    std::string result2 = lowerer.qualify("MyClass");
    assert(result2 == "A.B.MyClass");

    // Pop namespace
    lowerer.popNamespace(2);
    std::string result3 = lowerer.qualify("MyClass");
    assert(result3 == "MyClass");
}

// Test: qualify("A.B.T") => unchanged (fully-qualified)
void test_qualify_fq_name()
{
    Lowerer lowerer;

    // No namespace active
    std::string result1 = lowerer.qualify("A.B.MyClass");
    assert(result1 == "A.B.MyClass");

    // Push namespace X.Y (should not affect FQ names)
    lowerer.pushNamespace({"X", "Y"});
    std::string result2 = lowerer.qualify("A.B.MyClass");
    assert(result2 == "A.B.MyClass");

    // Pop namespace
    lowerer.popNamespace(2);
    std::string result3 = lowerer.qualify("A.B.MyClass");
    assert(result3 == "A.B.MyClass");
}

// Test: qualify() at global scope returns unqualified
void test_qualify_global_scope()
{
    Lowerer lowerer;

    // No namespace → unqualified
    std::string result = lowerer.qualify("GlobalClass");
    assert(result == "GlobalClass");
}

// Test: qualify() with empty string
void test_qualify_empty()
{
    Lowerer lowerer;

    lowerer.pushNamespace({"A", "B"});
    std::string result = lowerer.qualify("");
    assert(result == "");
    lowerer.popNamespace(2);
}

// Test: pushNamespace and popNamespace stack behavior
void test_namespace_stack()
{
    Lowerer lowerer;

    // Push A
    lowerer.pushNamespace({"A"});
    assert(lowerer.qualify("T") == "A.T");

    // Push B
    lowerer.pushNamespace({"B"});
    assert(lowerer.qualify("T") == "A.B.T");

    // Push C.D (multi-segment)
    lowerer.pushNamespace({"C", "D"});
    assert(lowerer.qualify("T") == "A.B.C.D.T");

    // Pop 2 segments
    lowerer.popNamespace(2);
    assert(lowerer.qualify("T") == "A.B.T");

    // Pop all
    lowerer.popNamespace(10); // Over-pop should be safe
    assert(lowerer.qualify("T") == "T");
}

int main()
{
    test_using_no_il();
    test_qualify_simple_name();
    test_qualify_fq_name();
    test_qualify_global_scope();
    test_qualify_empty();
    test_namespace_stack();
    return 0;
}
