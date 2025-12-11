//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SymbolTableOOPTests.cpp
// Purpose: Tests for Pascal OOP symbol table registration and lookup.
// Key invariants: Verifies classes, interfaces, methods, fields are properly
//                 registered and discoverable via the SemanticAnalyzer.
// Ownership/Lifetime: Test suite.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../../GTestStub.hpp"
#endif

#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/Lexer.hpp"
#include "frontends/pascal/Parser.hpp"
#include "frontends/pascal/SemanticAnalyzer.hpp"
#include "support/diagnostics.hpp"
#include <memory>
#include <string>

using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

//===----------------------------------------------------------------------===//
// Test Helpers
//===----------------------------------------------------------------------===//

/// @brief Parse and analyze a program, returning the analyzer for inspection.
std::unique_ptr<SemanticAnalyzer> analyzeAndReturn(const std::string &source,
                                                    DiagnosticEngine &diag,
                                                    bool &success)
{
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    if (!prog || parser.hasError())
    {
        success = false;
        return nullptr;
    }

    auto analyzer = std::make_unique<SemanticAnalyzer>(diag);
    success = analyzer->analyze(*prog);
    return analyzer;
}

bool analyzeProgram(const std::string &source, DiagnosticEngine &diag)
{
    bool success;
    analyzeAndReturn(source, diag, success);
    return success;
}

//===----------------------------------------------------------------------===//
// Class Symbol Registration Tests
//===----------------------------------------------------------------------===//

TEST(PascalSymbolTableOOP, ClassRegisteredAsType)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  TMyClass = class\n"
                                     "    x: Integer;\n"
                                     "  end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    // Class should be registered as a type
    auto typeOpt = analyzer->lookupType("TMyClass");
    ASSERT_TRUE(typeOpt.has_value());
    EXPECT_EQ(typeOpt->kind, PasTypeKind::Class);
    EXPECT_EQ(typeOpt->name, "TMyClass");

    // Should also be discoverable via lookupClass
    const ClassInfo *classInfo = analyzer->lookupClass("TMyClass");
    ASSERT_NE(classInfo, nullptr);
    EXPECT_EQ(classInfo->name, "TMyClass");
}

TEST(PascalSymbolTableOOP, ClassCaseInsensitiveLookup)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type TMyClass = class x: Integer; end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    // Case-insensitive lookup
    EXPECT_NE(analyzer->lookupClass("TMyClass"), nullptr);
    EXPECT_NE(analyzer->lookupClass("tmyclass"), nullptr);
    EXPECT_NE(analyzer->lookupClass("TMYCLASS"), nullptr);
    EXPECT_NE(analyzer->lookupClass("tMyCLASS"), nullptr);
}

TEST(PascalSymbolTableOOP, ClassFieldsRegistered)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  TPoint = class\n"
                                     "  private\n"
                                     "    FX: Integer;\n"
                                     "    FY: Integer;\n"
                                     "  public\n"
                                     "    Name: String;\n"
                                     "  end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    const ClassInfo *info = analyzer->lookupClass("TPoint");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->fields.size(), 3u);

    // Fields stored with lowercase keys
    auto fxIt = info->fields.find("fx");
    ASSERT_NE(fxIt, info->fields.end());
    EXPECT_EQ(fxIt->second.name, "FX");
    EXPECT_EQ(fxIt->second.type.kind, PasTypeKind::Integer);
    EXPECT_EQ(fxIt->second.visibility, Visibility::Private);

    auto nameIt = info->fields.find("name");
    ASSERT_NE(nameIt, info->fields.end());
    EXPECT_EQ(nameIt->second.visibility, Visibility::Public);
}

TEST(PascalSymbolTableOOP, ClassMethodsRegistered)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  TCalc = class\n"
                                     "  public\n"
                                     "    function Add(a, b: Integer): Integer;\n"
                                     "    procedure Reset;\n"
                                     "  end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    const ClassInfo *info = analyzer->lookupClass("TCalc");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->methods.size(), 2u);

    auto addIt = info->methods.find("add");
    ASSERT_NE(addIt, info->methods.end());
    EXPECT_EQ(addIt->second.name, "Add");
    EXPECT_EQ(addIt->second.returnType.kind, PasTypeKind::Integer);
    EXPECT_EQ(addIt->second.params.size(), 2u);

    auto resetIt = info->methods.find("reset");
    ASSERT_NE(resetIt, info->methods.end());
    EXPECT_EQ(resetIt->second.returnType.kind, PasTypeKind::Void);
}

TEST(PascalSymbolTableOOP, ClassMethodModifiersRegistered)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  TBase = class\n"
                                     "    procedure VirtualMethod; virtual;\n"
                                     "    procedure AbstractMethod; virtual; abstract;\n"
                                     "  end;\n"
                                     "  TDerived = class(TBase)\n"
                                     "    procedure VirtualMethod; override;\n"
                                     "    procedure AbstractMethod; override;\n"
                                     "  end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    const ClassInfo *base = analyzer->lookupClass("TBase");
    ASSERT_NE(base, nullptr);
    auto vmIt = base->methods.find("virtualmethod");
    ASSERT_NE(vmIt, base->methods.end());
    EXPECT_TRUE(vmIt->second.isVirtual);
    EXPECT_FALSE(vmIt->second.isAbstract);

    auto amIt = base->methods.find("abstractmethod");
    ASSERT_NE(amIt, base->methods.end());
    EXPECT_TRUE(amIt->second.isVirtual);
    EXPECT_TRUE(amIt->second.isAbstract);

    const ClassInfo *derived = analyzer->lookupClass("TDerived");
    ASSERT_NE(derived, nullptr);
    auto dvmIt = derived->methods.find("virtualmethod");
    ASSERT_NE(dvmIt, derived->methods.end());
    EXPECT_TRUE(dvmIt->second.isOverride);
}

TEST(PascalSymbolTableOOP, ClassConstructorDestructorRegistered)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  TResource = class\n"
                                     "    constructor Create(name: String);\n"
                                     "    destructor Destroy;\n"
                                     "  end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    const ClassInfo *info = analyzer->lookupClass("TResource");
    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(info->hasConstructor);
    EXPECT_TRUE(info->hasDestructor);

    // Constructor is registered as a method named "Create"
    auto ctorIt = info->methods.find("create");
    ASSERT_NE(ctorIt, info->methods.end());
    EXPECT_EQ(ctorIt->second.params.size(), 1u);

    // Destructor is registered as a method named "Destroy"
    auto dtorIt = info->methods.find("destroy");
    ASSERT_NE(dtorIt, info->methods.end());
}

TEST(PascalSymbolTableOOP, ClassInheritanceTracked)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  TBase = class x: Integer; end;\n"
                                     "  TChild = class(TBase) y: Integer; end;\n"
                                     "  TGrandchild = class(TChild) z: Integer; end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    const ClassInfo *child = analyzer->lookupClass("TChild");
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->baseClass, "TBase");

    const ClassInfo *grandchild = analyzer->lookupClass("TGrandchild");
    ASSERT_NE(grandchild, nullptr);
    EXPECT_EQ(grandchild->baseClass, "TChild");
}

TEST(PascalSymbolTableOOP, ClassAbstractnessDetected)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  TAbstract = class\n"
                                     "    procedure DoWork; virtual; abstract;\n"
                                     "  end;\n"
                                     "  TConcrete = class(TAbstract)\n"
                                     "    procedure DoWork; override;\n"
                                     "  end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    const ClassInfo *abstract = analyzer->lookupClass("TAbstract");
    ASSERT_NE(abstract, nullptr);
    EXPECT_TRUE(abstract->isAbstract);

    const ClassInfo *concrete = analyzer->lookupClass("TConcrete");
    ASSERT_NE(concrete, nullptr);
    EXPECT_FALSE(concrete->isAbstract);
}

//===----------------------------------------------------------------------===//
// Interface Symbol Registration Tests
//===----------------------------------------------------------------------===//

TEST(PascalSymbolTableOOP, InterfaceRegisteredAsType)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  IDrawable = interface\n"
                                     "    procedure Draw;\n"
                                     "  end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    // Interface should be registered as a type
    auto typeOpt = analyzer->lookupType("IDrawable");
    ASSERT_TRUE(typeOpt.has_value());
    EXPECT_EQ(typeOpt->kind, PasTypeKind::Interface);

    // Should also be discoverable via lookupInterface
    const InterfaceInfo *info = analyzer->lookupInterface("IDrawable");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->name, "IDrawable");
}

TEST(PascalSymbolTableOOP, InterfaceMethodsRegistered)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  IShape = interface\n"
                                     "    function GetArea: Real;\n"
                                     "    procedure Draw;\n"
                                     "    function Contains(x, y: Integer): Boolean;\n"
                                     "  end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    const InterfaceInfo *info = analyzer->lookupInterface("IShape");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->methods.size(), 3u);

    auto areaIt = info->methods.find("getarea");
    ASSERT_NE(areaIt, info->methods.end());
    EXPECT_EQ(areaIt->second.returnType.kind, PasTypeKind::Real);

    auto containsIt = info->methods.find("contains");
    ASSERT_NE(containsIt, info->methods.end());
    EXPECT_EQ(containsIt->second.params.size(), 2u);
}

TEST(PascalSymbolTableOOP, InterfaceInheritanceTracked)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  IBase = interface procedure Base; end;\n"
                                     "  IDerived = interface(IBase) procedure Derived; end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    const InterfaceInfo *derived = analyzer->lookupInterface("IDerived");
    ASSERT_NE(derived, nullptr);
    EXPECT_EQ(derived->baseInterfaces.size(), 1u);
    EXPECT_EQ(derived->baseInterfaces[0], "IBase");
}

TEST(PascalSymbolTableOOP, ClassInterfaceImplementationTracked)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  IFoo = interface procedure Foo; end;\n"
                                     "  IBar = interface procedure Bar; end;\n"
                                     "  TImpl = class(IFoo, IBar)\n"
                                     "    procedure Foo;\n"
                                     "    procedure Bar;\n"
                                     "  end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    const ClassInfo *impl = analyzer->lookupClass("TImpl");
    ASSERT_NE(impl, nullptr);
    EXPECT_EQ(impl->interfaces.size(), 2u);
    EXPECT_EQ(impl->interfaces[0], "IFoo");
    EXPECT_EQ(impl->interfaces[1], "IBar");
}

//===----------------------------------------------------------------------===//
// Property Symbol Registration Tests
//===----------------------------------------------------------------------===//

TEST(PascalSymbolTableOOP, PropertyRegistered)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  TCounter = class\n"
                                     "  private\n"
                                     "    FValue: Integer;\n"
                                     "  public\n"
                                     "    property Value: Integer read FValue write FValue;\n"
                                     "  end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    const ClassInfo *info = analyzer->lookupClass("TCounter");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->properties.size(), 1u);

    auto propIt = info->properties.find("value");
    ASSERT_NE(propIt, info->properties.end());
    EXPECT_EQ(propIt->second.name, "Value");
    EXPECT_EQ(propIt->second.type.kind, PasTypeKind::Integer);
    EXPECT_EQ(propIt->second.getter.kind, PropertyAccessor::Kind::Field);
    EXPECT_EQ(propIt->second.getter.name, "FValue");
    EXPECT_EQ(propIt->second.setter.kind, PropertyAccessor::Kind::Field);
}

TEST(PascalSymbolTableOOP, PropertyMethodAccessors)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  TCounter = class\n"
                                     "  private\n"
                                     "    FValue: Integer;\n"
                                     "    function GetValue: Integer;\n"
                                     "    procedure SetValue(v: Integer);\n"
                                     "  public\n"
                                     "    property Value: Integer read GetValue write SetValue;\n"
                                     "  end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    const ClassInfo *info = analyzer->lookupClass("TCounter");
    ASSERT_NE(info, nullptr);

    auto propIt = info->properties.find("value");
    ASSERT_NE(propIt, info->properties.end());
    EXPECT_EQ(propIt->second.getter.kind, PropertyAccessor::Kind::Method);
    EXPECT_EQ(propIt->second.getter.name, "GetValue");
    EXPECT_EQ(propIt->second.setter.kind, PropertyAccessor::Kind::Method);
    EXPECT_EQ(propIt->second.setter.name, "SetValue");
}

//===----------------------------------------------------------------------===//
// Weak Field Symbol Tests
//===----------------------------------------------------------------------===//

TEST(PascalSymbolTableOOP, WeakFieldMarked)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  TNode = class\n"
                                     "    Next: TNode;\n"
                                     "    weak Prev: TNode;\n"
                                     "  end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    const ClassInfo *info = analyzer->lookupClass("TNode");
    ASSERT_NE(info, nullptr);

    auto nextIt = info->fields.find("next");
    ASSERT_NE(nextIt, info->fields.end());
    EXPECT_FALSE(nextIt->second.isWeak);

    auto prevIt = info->fields.find("prev");
    ASSERT_NE(prevIt, info->fields.end());
    EXPECT_TRUE(prevIt->second.isWeak);
}

//===----------------------------------------------------------------------===//
// Multiple Scope Tests
//===----------------------------------------------------------------------===//

TEST(PascalSymbolTableOOP, MultipleClassesInSameScope)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  TFirst = class a: Integer; end;\n"
                                     "  TSecond = class b: String; end;\n"
                                     "  TThird = class c: Real; end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    EXPECT_NE(analyzer->lookupClass("TFirst"), nullptr);
    EXPECT_NE(analyzer->lookupClass("TSecond"), nullptr);
    EXPECT_NE(analyzer->lookupClass("TThird"), nullptr);
}

TEST(PascalSymbolTableOOP, ClassesAndInterfacesCoexist)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  IRunnable = interface procedure Run; end;\n"
                                     "  TRunner = class(IRunnable)\n"
                                     "    procedure Run;\n"
                                     "  end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    EXPECT_NE(analyzer->lookupInterface("IRunnable"), nullptr);
    EXPECT_NE(analyzer->lookupClass("TRunner"), nullptr);

    // Both are also registered as types
    auto ifaceType = analyzer->lookupType("IRunnable");
    ASSERT_TRUE(ifaceType.has_value());
    EXPECT_EQ(ifaceType->kind, PasTypeKind::Interface);

    auto classType = analyzer->lookupType("TRunner");
    ASSERT_TRUE(classType.has_value());
    EXPECT_EQ(classType->kind, PasTypeKind::Class);
}

//===----------------------------------------------------------------------===//
// Error Detection Tests
//===----------------------------------------------------------------------===//

TEST(PascalSymbolTableOOP, ConstantShadowsEnumConstant)
{
    // Note: Pascal allows const declarations to shadow enum constants.
    // This test documents the current behavior.
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TColor = (Red, Green, Blue);\n"
                                 "const\n"
                                 "  Red = 1;\n" // Shadows enum constant
                                 "begin end.",
                                 diag);
    EXPECT_TRUE(result); // Shadowing is allowed
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSymbolTableOOP, ExceptionClassRedefinitionError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  Exception = class x: Integer; end;\n" // Built-in
                                 "begin end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSymbolTableOOP, UnknownBaseClassError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TChild = class(TNonExistent) x: Integer; end;\n"
                                 "begin end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSymbolTableOOP, UnknownInterfaceError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TImpl = class(INonExistent) procedure Foo; end;\n"
                                 "begin end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Integration with Non-OOP Types Tests
//===----------------------------------------------------------------------===//

TEST(PascalSymbolTableOOP, ClassFieldWithRecordType)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  TPoint = record X, Y: Integer; end;\n"
                                     "  TShape = class\n"
                                     "    Position: TPoint;\n"
                                     "  end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    const ClassInfo *info = analyzer->lookupClass("TShape");
    ASSERT_NE(info, nullptr);

    auto posIt = info->fields.find("position");
    ASSERT_NE(posIt, info->fields.end());
    EXPECT_EQ(posIt->second.type.kind, PasTypeKind::Record);
}

TEST(PascalSymbolTableOOP, ClassFieldWithEnumType)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  TState = (Running, Paused, Stopped);\n"
                                     "  TTask = class\n"
                                     "    State: TState;\n"
                                     "  end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    const ClassInfo *info = analyzer->lookupClass("TTask");
    ASSERT_NE(info, nullptr);

    auto stateIt = info->fields.find("state");
    ASSERT_NE(stateIt, info->fields.end());
    EXPECT_EQ(stateIt->second.type.kind, PasTypeKind::Enum);
}

TEST(PascalSymbolTableOOP, ClassFieldWithArrayType)
{
    DiagnosticEngine diag;
    bool success;
    auto analyzer = analyzeAndReturn("program Test;\n"
                                     "type\n"
                                     "  TContainer = class\n"
                                     "    Items: array of Integer;\n"
                                     "  end;\n"
                                     "begin end.",
                                     diag, success);
    EXPECT_TRUE(success);
    ASSERT_NE(analyzer, nullptr);

    const ClassInfo *info = analyzer->lookupClass("TContainer");
    ASSERT_NE(info, nullptr);

    auto itemsIt = info->fields.find("items");
    ASSERT_NE(itemsIt, info->fields.end());
    EXPECT_EQ(itemsIt->second.type.kind, PasTypeKind::Array);
}

} // namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
