//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/ParserOOPTests.cpp
// Purpose: Comprehensive tests for Pascal OOP grammar coverage.
// Key invariants: Verifies all OOP constructs from ViperPascal spec.
// Ownership/Lifetime: Test suite.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//
#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/Lexer.hpp"
#include "frontends/pascal/Parser.hpp"
#include "support/diagnostics.hpp"
#include "tests/TestHarness.hpp"
#include <memory>
#include <string>

using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

//===----------------------------------------------------------------------===//
// Test Helpers
//===----------------------------------------------------------------------===//

std::unique_ptr<Program> parseProg(const std::string &source)
{
    DiagnosticEngine diag;
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    return parser.parseProgram();
}

std::unique_ptr<Expr> parseExpr(const std::string &source)
{
    DiagnosticEngine diag;
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    return parser.parseExpression();
}

std::unique_ptr<Stmt> parseStmt(const std::string &source)
{
    DiagnosticEngine diag;
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    return parser.parseStatement();
}

bool hasParseError(const std::string &source)
{
    DiagnosticEngine diag;
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    parser.parseProgram();
    return parser.hasError();
}

template <typename T> T *asDecl(Decl *d)
{
    return dynamic_cast<T *>(d);
}

template <typename T> T *asExpr(Expr *e)
{
    return dynamic_cast<T *>(e);
}

template <typename T> T *asStmt(Stmt *s)
{
    return dynamic_cast<T *>(s);
}

//===----------------------------------------------------------------------===//
// Class Declaration Tests - Basic Structure
//===----------------------------------------------------------------------===//

TEST(PascalOOPParser, ClassEmptyBody)
{
    auto prog = parseProg("program Test;\n"
                          "type TEmpty = class end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);
    ASSERT_EQ(prog->decls.size(), 1u);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->name, "TEmpty");
    EXPECT_TRUE(cls->baseClass.empty());
    EXPECT_TRUE(cls->interfaces.empty());
    EXPECT_TRUE(cls->members.empty());
}

TEST(PascalOOPParser, ClassWithSingleInheritance)
{
    auto prog = parseProg("program Test;\n"
                          "type TChild = class(TParent) end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->name, "TChild");
    EXPECT_EQ(cls->baseClass, "TParent");
    EXPECT_TRUE(cls->interfaces.empty());
}

TEST(PascalOOPParser, ClassWithMultipleInterfaces)
{
    auto prog = parseProg("program Test;\n"
                          "type TWidget = class(TBase, IDrawable, IResizable, IClickable)\n"
                          "end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->baseClass, "TBase");
    EXPECT_EQ(cls->interfaces.size(), 3u);
    EXPECT_EQ(cls->interfaces[0], "IDrawable");
    EXPECT_EQ(cls->interfaces[1], "IResizable");
    EXPECT_EQ(cls->interfaces[2], "IClickable");
}

TEST(PascalOOPParser, ClassOnlyInterfaces)
{
    // Class implementing interfaces without explicit base class
    auto prog = parseProg("program Test;\n"
                          "type TImpl = class(IFoo, IBar)\n"
                          "end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    ASSERT_NE(cls, nullptr);
    // Heuristic: I-prefixed names are interfaces
    EXPECT_TRUE(cls->baseClass.empty());
    EXPECT_EQ(cls->interfaces.size(), 2u);
}

//===----------------------------------------------------------------------===//
// Field Declaration Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPParser, ClassCommaFieldDeclaration)
{
    // Multiple fields on same line: x, y, z: Type;
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TPoint3D = class\n"
                          "    x, y, z: Real;\n"
                          "  end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->members.size(), 3u);
    EXPECT_EQ(cls->members[0].fieldName, "x");
    EXPECT_EQ(cls->members[1].fieldName, "y");
    EXPECT_EQ(cls->members[2].fieldName, "z");
}

TEST(PascalOOPParser, ClassWeakFieldDeclaration)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TLinkedNode = class\n"
                          "    next: TLinkedNode;\n"
                          "    weak prev: TLinkedNode;\n"
                          "  end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->members.size(), 2u);
    EXPECT_FALSE(cls->members[0].isWeak);
    EXPECT_TRUE(cls->members[1].isWeak);
}

TEST(PascalOOPParser, ClassMixedVisibility)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TPerson = class\n"
                          "  private\n"
                          "    FName: String;\n"
                          "    FAge: Integer;\n"
                          "  public\n"
                          "    Address: String;\n"
                          "  private\n"
                          "    FSecret: String;\n"
                          "  public\n"
                          "    procedure Print;\n"
                          "  end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->members.size(), 5u);
    EXPECT_EQ(cls->members[0].visibility, Visibility::Private);
    EXPECT_EQ(cls->members[1].visibility, Visibility::Private);
    EXPECT_EQ(cls->members[2].visibility, Visibility::Public);
    EXPECT_EQ(cls->members[3].visibility, Visibility::Private);
    EXPECT_EQ(cls->members[4].visibility, Visibility::Public);
}

//===----------------------------------------------------------------------===//
// Method Signature Tests - Modifiers
//===----------------------------------------------------------------------===//

TEST(PascalOOPParser, MethodVirtualModifier)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TBase = class\n"
                          "    procedure DoWork; virtual;\n"
                          "  end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->members.size(), 1u);
    EXPECT_EQ(cls->members[0].memberKind, ClassMember::Kind::Method);

    auto *proc = asDecl<ProcedureDecl>(cls->members[0].methodDecl.get());
    ASSERT_NE(proc, nullptr);
    EXPECT_TRUE(proc->isVirtual);
    EXPECT_FALSE(proc->isOverride);
    EXPECT_FALSE(proc->isAbstract);
}

TEST(PascalOOPParser, MethodOverrideModifier)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TChild = class(TBase)\n"
                          "    procedure DoWork; override;\n"
                          "  end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    ASSERT_NE(cls, nullptr);

    auto *proc = asDecl<ProcedureDecl>(cls->members[0].methodDecl.get());
    ASSERT_NE(proc, nullptr);
    EXPECT_FALSE(proc->isVirtual);
    EXPECT_TRUE(proc->isOverride);
    EXPECT_FALSE(proc->isAbstract);
}

TEST(PascalOOPParser, MethodAbstractModifier)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TAbstractShape = class\n"
                          "    function GetArea: Real; virtual; abstract;\n"
                          "  end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    ASSERT_NE(cls, nullptr);

    auto *func = asDecl<FunctionDecl>(cls->members[0].methodDecl.get());
    ASSERT_NE(func, nullptr);
    EXPECT_TRUE(func->isVirtual);
    EXPECT_FALSE(func->isOverride);
    EXPECT_TRUE(func->isAbstract);
}

TEST(PascalOOPParser, MethodCombinedModifiers)
{
    // virtual; abstract; combination is valid per spec
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TAbstract = class\n"
                          "    procedure Method1; virtual; abstract;\n"
                          "  end;\n"
                          "  TConcrete = class(TAbstract)\n"
                          "    procedure Method1; override;\n"
                          "  end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *abstract = asDecl<ClassDecl>(prog->decls[0].get());
    auto *concrete = asDecl<ClassDecl>(prog->decls[1].get());
    ASSERT_NE(abstract, nullptr);
    ASSERT_NE(concrete, nullptr);

    auto *m1 = asDecl<ProcedureDecl>(abstract->members[0].methodDecl.get());
    EXPECT_TRUE(m1->isVirtual);
    EXPECT_TRUE(m1->isAbstract);

    auto *m2 = asDecl<ProcedureDecl>(concrete->members[0].methodDecl.get());
    EXPECT_TRUE(m2->isOverride);
}

//===----------------------------------------------------------------------===//
// Constructor and Destructor Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPParser, ConstructorSignature)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TPoint = class\n"
                          "    constructor Create(x, y: Integer);\n"
                          "  end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->members[0].memberKind, ClassMember::Kind::Constructor);

    auto *ctor = asDecl<ConstructorDecl>(cls->members[0].methodDecl.get());
    ASSERT_NE(ctor, nullptr);
    EXPECT_EQ(ctor->name, "Create");
    EXPECT_EQ(ctor->params.size(), 2u);
}

TEST(PascalOOPParser, DestructorSignatureWithModifiers)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TBase = class\n"
                          "    destructor Destroy; virtual;\n"
                          "  end;\n"
                          "  TChild = class(TBase)\n"
                          "    destructor Destroy; override;\n"
                          "  end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *base = asDecl<ClassDecl>(prog->decls[0].get());
    auto *child = asDecl<ClassDecl>(prog->decls[1].get());

    auto *baseDtor = asDecl<DestructorDecl>(base->members[0].methodDecl.get());
    ASSERT_NE(baseDtor, nullptr);
    EXPECT_TRUE(baseDtor->isVirtual);
    EXPECT_FALSE(baseDtor->isOverride);

    auto *childDtor = asDecl<DestructorDecl>(child->members[0].methodDecl.get());
    ASSERT_NE(childDtor, nullptr);
    EXPECT_FALSE(childDtor->isVirtual);
    EXPECT_TRUE(childDtor->isOverride);
}

TEST(PascalOOPParser, ConstructorImplementation)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TPoint = class\n"
                          "    X, Y: Integer;\n"
                          "    constructor Create(ax, ay: Integer);\n"
                          "  end;\n"
                          "\n"
                          "constructor TPoint.Create(ax, ay: Integer);\n"
                          "begin\n"
                          "  X := ax;\n"
                          "  Y := ay\n"
                          "end;\n"
                          "\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);
    EXPECT_EQ(prog->decls.size(), 2u);

    // Second decl is the constructor implementation
    auto *ctorImpl = asDecl<ConstructorDecl>(prog->decls[1].get());
    ASSERT_NE(ctorImpl, nullptr);
    EXPECT_EQ(ctorImpl->className, "TPoint");
    EXPECT_EQ(ctorImpl->name, "Create");
    EXPECT_NE(ctorImpl->body, nullptr);
}

TEST(PascalOOPParser, DestructorImplementation)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TResource = class\n"
                          "    destructor Destroy;\n"
                          "  end;\n"
                          "\n"
                          "destructor TResource.Destroy;\n"
                          "begin\n"
                          "end;\n"
                          "\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);
    EXPECT_EQ(prog->decls.size(), 2u);

    auto *dtorImpl = asDecl<DestructorDecl>(prog->decls[1].get());
    ASSERT_NE(dtorImpl, nullptr);
    EXPECT_EQ(dtorImpl->className, "TResource");
    EXPECT_EQ(dtorImpl->name, "Destroy");
}

//===----------------------------------------------------------------------===//
// Method Implementation Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPParser, ProcedureMethodImplementation)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TCounter = class\n"
                          "    procedure Inc;\n"
                          "  end;\n"
                          "\n"
                          "procedure TCounter.Inc;\n"
                          "begin\n"
                          "end;\n"
                          "\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);
    EXPECT_EQ(prog->decls.size(), 2u);

    auto *procImpl = asDecl<ProcedureDecl>(prog->decls[1].get());
    ASSERT_NE(procImpl, nullptr);
    EXPECT_EQ(procImpl->className, "TCounter");
    EXPECT_EQ(procImpl->name, "Inc");
    EXPECT_NE(procImpl->body, nullptr);
}

TEST(PascalOOPParser, FunctionMethodImplementation)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TCalculator = class\n"
                          "    function Add(a, b: Integer): Integer;\n"
                          "  end;\n"
                          "\n"
                          "function TCalculator.Add(a, b: Integer): Integer;\n"
                          "begin\n"
                          "  Result := a + b\n"
                          "end;\n"
                          "\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);
    EXPECT_EQ(prog->decls.size(), 2u);

    auto *funcImpl = asDecl<FunctionDecl>(prog->decls[1].get());
    ASSERT_NE(funcImpl, nullptr);
    EXPECT_EQ(funcImpl->className, "TCalculator");
    EXPECT_EQ(funcImpl->name, "Add");
    EXPECT_NE(funcImpl->body, nullptr);
}

TEST(PascalOOPParser, MethodWithLocalVariables)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TCalc = class\n"
                          "    function Compute(x: Integer): Integer;\n"
                          "  end;\n"
                          "\n"
                          "function TCalc.Compute(x: Integer): Integer;\n"
                          "var temp: Integer;\n"
                          "begin\n"
                          "  temp := x * 2;\n"
                          "  Result := temp + 1\n"
                          "end;\n"
                          "\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *funcImpl = asDecl<FunctionDecl>(prog->decls[1].get());
    ASSERT_NE(funcImpl, nullptr);
    EXPECT_EQ(funcImpl->localDecls.size(), 1u);
}

//===----------------------------------------------------------------------===//
// Interface Declaration Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPParser, InterfaceEmpty)
{
    auto prog = parseProg("program Test;\n"
                          "type IEmpty = interface end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *iface = asDecl<InterfaceDecl>(prog->decls[0].get());
    ASSERT_NE(iface, nullptr);
    EXPECT_EQ(iface->name, "IEmpty");
    EXPECT_TRUE(iface->baseInterfaces.empty());
    EXPECT_TRUE(iface->methods.empty());
}

TEST(PascalOOPParser, InterfaceWithMethods)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  IShape = interface\n"
                          "    procedure Draw;\n"
                          "    function GetArea: Real;\n"
                          "    function Contains(x, y: Integer): Boolean;\n"
                          "  end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *iface = asDecl<InterfaceDecl>(prog->decls[0].get());
    ASSERT_NE(iface, nullptr);
    EXPECT_EQ(iface->methods.size(), 3u);

    // Procedure (no return type)
    EXPECT_EQ(iface->methods[0].name, "Draw");
    EXPECT_EQ(iface->methods[0].returnType, nullptr);
    EXPECT_TRUE(iface->methods[0].params.empty());

    // Function with no params
    EXPECT_EQ(iface->methods[1].name, "GetArea");
    EXPECT_NE(iface->methods[1].returnType, nullptr);
    EXPECT_TRUE(iface->methods[1].params.empty());

    // Function with params
    EXPECT_EQ(iface->methods[2].name, "Contains");
    EXPECT_NE(iface->methods[2].returnType, nullptr);
    EXPECT_EQ(iface->methods[2].params.size(), 2u);
}

TEST(PascalOOPParser, InterfaceMultipleInheritance)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  IComposite = interface(IBase1, IBase2, IBase3)\n"
                          "    procedure DoComposite;\n"
                          "  end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *iface = asDecl<InterfaceDecl>(prog->decls[0].get());
    ASSERT_NE(iface, nullptr);
    EXPECT_EQ(iface->baseInterfaces.size(), 3u);
    EXPECT_EQ(iface->baseInterfaces[0], "IBase1");
    EXPECT_EQ(iface->baseInterfaces[1], "IBase2");
    EXPECT_EQ(iface->baseInterfaces[2], "IBase3");
}

//===----------------------------------------------------------------------===//
// Property Declaration Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPParser, PropertyReadOnly)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TReadOnly = class\n"
                          "  private\n"
                          "    FValue: Integer;\n"
                          "  public\n"
                          "    property Value: Integer read FValue;\n"
                          "  end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    ASSERT_NE(cls, nullptr);

    // Find property member
    const PropertyDecl *prop = nullptr;
    for (const auto &m : cls->members)
    {
        if (m.memberKind == ClassMember::Kind::Property && m.property)
        {
            prop = m.property.get();
            break;
        }
    }
    ASSERT_NE(prop, nullptr);
    EXPECT_EQ(prop->name, "Value");
    EXPECT_EQ(prop->getter, "FValue");
    EXPECT_TRUE(prop->setter.empty());
}

TEST(PascalOOPParser, PropertyReadWrite)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TCounter = class\n"
                          "  private\n"
                          "    FCount: Integer;\n"
                          "    function GetCount: Integer;\n"
                          "    procedure SetCount(value: Integer);\n"
                          "  public\n"
                          "    property Count: Integer read GetCount write SetCount;\n"
                          "  end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    ASSERT_NE(cls, nullptr);

    const PropertyDecl *prop = nullptr;
    for (const auto &m : cls->members)
    {
        if (m.memberKind == ClassMember::Kind::Property && m.property)
        {
            prop = m.property.get();
            break;
        }
    }
    ASSERT_NE(prop, nullptr);
    EXPECT_EQ(prop->getter, "GetCount");
    EXPECT_EQ(prop->setter, "SetCount");
}

//===----------------------------------------------------------------------===//
// Inherited Statement Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPParser, InheritedStatementImplicit)
{
    // 'inherited' without method name calls same method on parent
    auto stmt = parseStmt("inherited");
    ASSERT_NE(stmt, nullptr);

    auto *inherited = asStmt<InheritedStmt>(stmt.get());
    ASSERT_NE(inherited, nullptr);
    EXPECT_TRUE(inherited->methodName.empty());
    EXPECT_TRUE(inherited->args.empty());
}

TEST(PascalOOPParser, InheritedInMethodBody)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TBase = class\n"
                          "    procedure DoWork; virtual;\n"
                          "  end;\n"
                          "  TChild = class(TBase)\n"
                          "    procedure DoWork; override;\n"
                          "  end;\n"
                          "\n"
                          "procedure TBase.DoWork;\n"
                          "begin\n"
                          "end;\n"
                          "\n"
                          "procedure TChild.DoWork;\n"
                          "begin\n"
                          "  inherited\n"
                          "end;\n"
                          "\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);
    // Verify it parses without error
    EXPECT_EQ(prog->decls.size(), 4u);
}

//===----------------------------------------------------------------------===//
// Is Expression Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPParser, IsExpressionSimple)
{
    auto expr = parseExpr("obj is TChild");
    ASSERT_NE(expr, nullptr);

    auto *isExpr = asExpr<IsExpr>(expr.get());
    ASSERT_NE(isExpr, nullptr);

    auto *operand = asExpr<NameExpr>(isExpr->operand.get());
    ASSERT_NE(operand, nullptr);
    EXPECT_EQ(operand->name, "obj");

    auto *targetType = dynamic_cast<NamedTypeNode *>(isExpr->targetType.get());
    ASSERT_NE(targetType, nullptr);
    EXPECT_EQ(targetType->name, "TChild");
}

TEST(PascalOOPParser, IsExpressionInCondition)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TBase = class end;\n"
                          "  TChild = class(TBase) end;\n"
                          "var obj: TBase;\n"
                          "begin\n"
                          "  if obj is TChild then\n"
                          "    WriteLn('Is child')\n"
                          "end.");
    ASSERT_NE(prog, nullptr);
    EXPECT_FALSE(hasParseError("program Test;\n"
                               "var obj: TBase;\n"
                               "begin if obj is TChild then WriteLn('ok') end."));
}

TEST(PascalOOPParser, IsExpressionWithFieldAccess)
{
    auto expr = parseExpr("container.item is TSpecific");
    ASSERT_NE(expr, nullptr);

    auto *isExpr = asExpr<IsExpr>(expr.get());
    ASSERT_NE(isExpr, nullptr);

    auto *operand = asExpr<FieldExpr>(isExpr->operand.get());
    ASSERT_NE(operand, nullptr);
    EXPECT_EQ(operand->field, "item");
}

//===----------------------------------------------------------------------===//
// Complex OOP Pattern Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPParser, CompleteInheritanceHierarchy)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  IDrawable = interface\n"
                          "    procedure Draw;\n"
                          "  end;\n"
                          "\n"
                          "  TShape = class\n"
                          "  private\n"
                          "    FX, FY: Integer;\n"
                          "  public\n"
                          "    constructor Create(x, y: Integer);\n"
                          "    function GetArea: Real; virtual; abstract;\n"
                          "    property X: Integer read FX write FX;\n"
                          "    property Y: Integer read FY write FY;\n"
                          "  end;\n"
                          "\n"
                          "  TCircle = class(TShape, IDrawable)\n"
                          "  private\n"
                          "    FRadius: Real;\n"
                          "  public\n"
                          "    constructor Create(x, y: Integer; r: Real);\n"
                          "    destructor Destroy; override;\n"
                          "    procedure Draw;\n"
                          "    function GetArea: Real; override;\n"
                          "    property Radius: Real read FRadius write FRadius;\n"
                          "  end;\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);
    EXPECT_EQ(prog->decls.size(), 3u);

    // Verify IDrawable
    auto *iface = asDecl<InterfaceDecl>(prog->decls[0].get());
    ASSERT_NE(iface, nullptr);
    EXPECT_EQ(iface->name, "IDrawable");

    // Verify TShape
    auto *shape = asDecl<ClassDecl>(prog->decls[1].get());
    ASSERT_NE(shape, nullptr);
    EXPECT_EQ(shape->name, "TShape");

    // Verify TCircle inherits TShape and implements IDrawable
    auto *circle = asDecl<ClassDecl>(prog->decls[2].get());
    ASSERT_NE(circle, nullptr);
    EXPECT_EQ(circle->name, "TCircle");
    EXPECT_EQ(circle->baseClass, "TShape");
    EXPECT_EQ(circle->interfaces.size(), 1u);
    EXPECT_EQ(circle->interfaces[0], "IDrawable");
}

TEST(PascalOOPParser, MultipleClassesAndMethods)
{
    auto prog = parseProg("program Test;\n"
                          "type\n"
                          "  TOne = class\n"
                          "    procedure M1;\n"
                          "  end;\n"
                          "  TTwo = class\n"
                          "    procedure M2;\n"
                          "  end;\n"
                          "\n"
                          "procedure TOne.M1;\n"
                          "begin end;\n"
                          "\n"
                          "procedure TTwo.M2;\n"
                          "begin end;\n"
                          "\n"
                          "begin end.");
    ASSERT_NE(prog, nullptr);
    EXPECT_EQ(prog->decls.size(), 4u);
}

//===----------------------------------------------------------------------===//
// Error Handling Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPParser, ErrorMissingClassEnd)
{
    EXPECT_TRUE(hasParseError("program Test;\n"
                              "type TBroken = class\n"
                              "  x: Integer;\n"
                              "begin end."));
}

TEST(PascalOOPParser, ErrorMissingInterfaceEnd)
{
    EXPECT_TRUE(hasParseError("program Test;\n"
                              "type IBroken = interface\n"
                              "  procedure Foo;\n"
                              "begin end."));
}

TEST(PascalOOPParser, ErrorInvalidModifierCombination)
{
    // override without virtual in base - parser accepts this, semantic analysis rejects
    auto prog = parseProg("program Test;\n"
                          "type TChild = class(TBase)\n"
                          "  procedure Foo; override;\n"
                          "end;\n"
                          "begin end.");
    // Parser should not error - semantic analysis catches this
    ASSERT_NE(prog, nullptr);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
