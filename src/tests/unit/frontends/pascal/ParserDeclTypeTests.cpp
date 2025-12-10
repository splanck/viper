//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/ParserDeclTypeTests.cpp
// Purpose: Unit tests for the Viper Pascal parser (declarations and types).
// Key invariants: Verifies declaration and type parsing.
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

/// @brief Helper to parse a type from source text.
std::unique_ptr<TypeNode> parseTypeHelper(const std::string &source)
{
    DiagnosticEngine diag;
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    return parser.parseType();
}

/// @brief Helper to parse a program from source text.
std::unique_ptr<Program> parseProg(const std::string &source)
{
    DiagnosticEngine diag;
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    return parser.parseProgram();
}

/// @brief Helper to parse a unit from source text.
std::unique_ptr<Unit> parseUnitHelper(const std::string &source)
{
    DiagnosticEngine diag;
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    return parser.parseUnit();
}

/// @brief Helper to check if parsing produces an error.
bool hasParseError(const std::string &source)
{
    DiagnosticEngine diag;
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    parser.parseType();
    return parser.hasError();
}

/// @brief Cast type node to specific type.
template <typename T>
T *asType(TypeNode *t)
{
    return dynamic_cast<T *>(t);
}

/// @brief Cast decl node to specific type.
template <typename T>
T *asDecl(Decl *d)
{
    return dynamic_cast<T *>(d);
}

//===----------------------------------------------------------------------===//
// Named Type Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserType, NamedType)
{
    auto type = parseTypeHelper("Integer");
    EXPECT_TRUE(type != nullptr);
    EXPECT_EQ(type->kind, TypeKind::Named);

    auto *named = asType<NamedTypeNode>(type.get());
    EXPECT_TRUE(named != nullptr);
    EXPECT_EQ(named->name, "Integer");
}

TEST(PascalParserType, NamedTypeString)
{
    auto type = parseTypeHelper("String");
    EXPECT_TRUE(type != nullptr);

    auto *named = asType<NamedTypeNode>(type.get());
    EXPECT_TRUE(named != nullptr);
    EXPECT_EQ(named->name, "String");
}

//===----------------------------------------------------------------------===//
// Optional Type Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserType, OptionalType)
{
    auto type = parseTypeHelper("Integer?");
    EXPECT_TRUE(type != nullptr);
    EXPECT_EQ(type->kind, TypeKind::Optional);

    auto *opt = asType<OptionalTypeNode>(type.get());
    EXPECT_TRUE(opt != nullptr);
    EXPECT_TRUE(opt->inner != nullptr);

    auto *inner = asType<NamedTypeNode>(opt->inner.get());
    EXPECT_TRUE(inner != nullptr);
    EXPECT_EQ(inner->name, "Integer");
}

TEST(PascalParserType, DoubleOptionalRejected)
{
    // T?? should be rejected
    EXPECT_TRUE(hasParseError("Integer??"));
}

//===----------------------------------------------------------------------===//
// Enum Type Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserType, EnumType)
{
    auto type = parseTypeHelper("(Red, Green, Blue)");
    EXPECT_TRUE(type != nullptr);
    EXPECT_EQ(type->kind, TypeKind::Enum);

    auto *enumType = asType<EnumTypeNode>(type.get());
    EXPECT_TRUE(enumType != nullptr);
    EXPECT_EQ(enumType->values.size(), 3u);
    EXPECT_EQ(enumType->values[0], "Red");
    EXPECT_EQ(enumType->values[1], "Green");
    EXPECT_EQ(enumType->values[2], "Blue");
}

TEST(PascalParserType, EnumTypeSingle)
{
    auto type = parseTypeHelper("(North)");
    EXPECT_TRUE(type != nullptr);

    auto *enumType = asType<EnumTypeNode>(type.get());
    EXPECT_TRUE(enumType != nullptr);
    EXPECT_EQ(enumType->values.size(), 1u);
    EXPECT_EQ(enumType->values[0], "North");
}

//===----------------------------------------------------------------------===//
// Array Type Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserType, StaticArraySingleDim)
{
    // array[10] of Integer
    auto type = parseTypeHelper("array[10] of Integer");
    EXPECT_TRUE(type != nullptr);
    EXPECT_EQ(type->kind, TypeKind::Array);

    auto *arr = asType<ArrayTypeNode>(type.get());
    EXPECT_TRUE(arr != nullptr);
    EXPECT_EQ(arr->dimensions.size(), 1u);
    EXPECT_TRUE(arr->elementType != nullptr);

    auto *elem = asType<NamedTypeNode>(arr->elementType.get());
    EXPECT_EQ(elem->name, "Integer");
}

TEST(PascalParserType, DynamicArray)
{
    // array of String
    auto type = parseTypeHelper("array of String");
    EXPECT_TRUE(type != nullptr);

    auto *arr = asType<ArrayTypeNode>(type.get());
    EXPECT_TRUE(arr != nullptr);
    EXPECT_TRUE(arr->dimensions.empty());

    auto *elem = asType<NamedTypeNode>(arr->elementType.get());
    EXPECT_EQ(elem->name, "String");
}

TEST(PascalParserType, StaticArrayWithSize)
{
    // array[10] of Real - 0-based array with 10 elements
    auto type = parseTypeHelper("array[10] of Real");
    EXPECT_TRUE(type != nullptr);

    auto *arr = asType<ArrayTypeNode>(type.get());
    EXPECT_TRUE(arr != nullptr);
    EXPECT_EQ(arr->dimensions.size(), 1u);
    EXPECT_TRUE(arr->dimensions[0].size != nullptr);
}

TEST(PascalParserType, MatrixArray)
{
    // array[3, 4] of Real
    auto type = parseTypeHelper("array[3, 4] of Real");
    EXPECT_TRUE(type != nullptr);

    auto *arr = asType<ArrayTypeNode>(type.get());
    EXPECT_TRUE(arr != nullptr);
    EXPECT_EQ(arr->dimensions.size(), 2u);
}

//===----------------------------------------------------------------------===//
// Property Parsing in Class Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserDecl, ClassPropertySimple)
{
    const std::string src = R"(
type
  TPerson = class
  private
    FAge: Integer;
  public
    function GetAge: Integer;
    procedure SetAge(Value: Integer);
    property Age: Integer read GetAge write SetAge;
    property RawAge: Integer read FAge write FAge;
  end;
)";
    auto prog = parseProg("program P;\n" + src + "begin end.");
    ASSERT_TRUE(prog != nullptr);
    ASSERT_FALSE(prog->decls.empty());

    // Find the class decl
    ClassDecl *cls = nullptr;
    for (auto &d : prog->decls)
    {
        if (auto *cd = asDecl<ClassDecl>(d.get()))
        {
            cls = cd;
            break;
        }
    }
    ASSERT_TRUE(cls != nullptr);

    // Expect two properties among members
    size_t propCount = 0;
    for (const auto &m : cls->members)
    {
        if (m.memberKind == ClassMember::Kind::Property && m.property)
            ++propCount;
    }
    EXPECT_EQ(propCount, 2u);
}

TEST(PascalParserType, RangeSyntaxRejected)
{
    // array[0..2] of Real - range syntax is not supported in v0.1
    il::support::DiagnosticEngine diag;
    Lexer lexer("array[0..2] of Real", 0, diag);
    Parser parser(lexer, diag);
    auto type = parser.parseType();
    // Parser should reject range syntax
    EXPECT_TRUE(type == nullptr || parser.hasError());
}

TEST(PascalParserType, RangeSyntaxMultiDimRejected)
{
    // array[0..2, 0..3] of Real - range syntax is not supported in v0.1
    il::support::DiagnosticEngine diag;
    Lexer lexer("array[1..10, 1..20] of Integer", 0, diag);
    Parser parser(lexer, diag);
    auto type = parser.parseType();
    // Parser should reject range syntax
    EXPECT_TRUE(type == nullptr || parser.hasError());
}

//===----------------------------------------------------------------------===//
// Record Type Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserType, RecordType)
{
    auto type = parseTypeHelper("record x, y: Real; end");
    EXPECT_TRUE(type != nullptr);
    EXPECT_EQ(type->kind, TypeKind::Record);

    auto *rec = asType<RecordTypeNode>(type.get());
    EXPECT_TRUE(rec != nullptr);
    EXPECT_EQ(rec->fields.size(), 2u);
    EXPECT_EQ(rec->fields[0].name, "x");
    EXPECT_EQ(rec->fields[1].name, "y");
}

TEST(PascalParserType, RecordTypeMultipleFields)
{
    auto type = parseTypeHelper("record name: String; age: Integer; active: Boolean end");
    EXPECT_TRUE(type != nullptr);

    auto *rec = asType<RecordTypeNode>(type.get());
    EXPECT_TRUE(rec != nullptr);
    EXPECT_EQ(rec->fields.size(), 3u);
    EXPECT_EQ(rec->fields[0].name, "name");
    EXPECT_EQ(rec->fields[1].name, "age");
    EXPECT_EQ(rec->fields[2].name, "active");
}

//===----------------------------------------------------------------------===//
// Pointer Type Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserType, PointerType)
{
    auto type = parseTypeHelper("^Integer");
    EXPECT_TRUE(type != nullptr);
    EXPECT_EQ(type->kind, TypeKind::Pointer);

    auto *ptr = asType<PointerTypeNode>(type.get());
    EXPECT_TRUE(ptr != nullptr);

    auto *pointee = asType<NamedTypeNode>(ptr->pointeeType.get());
    EXPECT_EQ(pointee->name, "Integer");
}

//===----------------------------------------------------------------------===//
// Set Type Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserType, SetType)
{
    auto type = parseTypeHelper("set of Char");
    EXPECT_TRUE(type != nullptr);
    EXPECT_EQ(type->kind, TypeKind::Set);

    auto *setType = asType<SetTypeNode>(type.get());
    EXPECT_TRUE(setType != nullptr);

    auto *elem = asType<NamedTypeNode>(setType->elementType.get());
    EXPECT_EQ(elem->name, "Char");
}

//===----------------------------------------------------------------------===//
// Procedure/Function Type Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserType, ProcedureType)
{
    auto type = parseTypeHelper("procedure");
    EXPECT_TRUE(type != nullptr);
    EXPECT_EQ(type->kind, TypeKind::Procedure);

    auto *proc = asType<ProcedureTypeNode>(type.get());
    EXPECT_TRUE(proc != nullptr);
    EXPECT_TRUE(proc->params.empty());
}

TEST(PascalParserType, ProcedureTypeWithParams)
{
    auto type = parseTypeHelper("procedure(x: Integer; y: String)");
    EXPECT_TRUE(type != nullptr);

    auto *proc = asType<ProcedureTypeNode>(type.get());
    EXPECT_TRUE(proc != nullptr);
    EXPECT_EQ(proc->params.size(), 2u);
}

TEST(PascalParserType, FunctionType)
{
    auto type = parseTypeHelper("function: Boolean");
    EXPECT_TRUE(type != nullptr);
    EXPECT_EQ(type->kind, TypeKind::Function);

    auto *func = asType<FunctionTypeNode>(type.get());
    EXPECT_TRUE(func != nullptr);
    EXPECT_TRUE(func->params.empty());
    EXPECT_TRUE(func->returnType != nullptr);
}

TEST(PascalParserType, FunctionTypeWithParams)
{
    auto type = parseTypeHelper("function(x, y: Integer): Real");
    EXPECT_TRUE(type != nullptr);

    auto *func = asType<FunctionTypeNode>(type.get());
    EXPECT_TRUE(func != nullptr);
    EXPECT_EQ(func->params.size(), 2u);
}

//===----------------------------------------------------------------------===//
// Const Section Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserDecl, ConstSection)
{
    auto prog = parseProg(
        "program Test;\n"
        "const\n"
        "  PI = 3.14159;\n"
        "  MaxSize = 100;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->decls.size(), 2u);

    auto *c1 = asDecl<ConstDecl>(prog->decls[0].get());
    EXPECT_TRUE(c1 != nullptr);
    EXPECT_EQ(c1->name, "PI");

    auto *c2 = asDecl<ConstDecl>(prog->decls[1].get());
    EXPECT_TRUE(c2 != nullptr);
    EXPECT_EQ(c2->name, "MaxSize");
}

TEST(PascalParserDecl, TypedConst)
{
    auto prog = parseProg(
        "program Test;\n"
        "const\n"
        "  Name: String = 'Test';\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->decls.size(), 1u);

    auto *c = asDecl<ConstDecl>(prog->decls[0].get());
    EXPECT_TRUE(c != nullptr);
    EXPECT_EQ(c->name, "Name");
    EXPECT_TRUE(c->type != nullptr);
}

//===----------------------------------------------------------------------===//
// Type Section Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserDecl, TypeSectionEnum)
{
    auto prog = parseProg(
        "program Test;\n"
        "type\n"
        "  Color = (Red, Green, Blue);\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->decls.size(), 1u);

    auto *td = asDecl<TypeDecl>(prog->decls[0].get());
    EXPECT_TRUE(td != nullptr);
    EXPECT_EQ(td->name, "Color");

    auto *enumType = asType<EnumTypeNode>(td->type.get());
    EXPECT_TRUE(enumType != nullptr);
    EXPECT_EQ(enumType->values.size(), 3u);
}

TEST(PascalParserDecl, TypeSectionArray)
{
    auto prog = parseProg(
        "program Test;\n"
        "type\n"
        "  IntArray = array[10] of Integer;\n"
        "  Matrix = array[3, 4] of Real;\n"
        "  DynStrings = array of String;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->decls.size(), 3u);

    // IntArray
    auto *t1 = asDecl<TypeDecl>(prog->decls[0].get());
    EXPECT_EQ(t1->name, "IntArray");
    auto *arr1 = asType<ArrayTypeNode>(t1->type.get());
    EXPECT_EQ(arr1->dimensions.size(), 1u);

    // Matrix
    auto *t2 = asDecl<TypeDecl>(prog->decls[1].get());
    EXPECT_EQ(t2->name, "Matrix");
    auto *arr2 = asType<ArrayTypeNode>(t2->type.get());
    EXPECT_EQ(arr2->dimensions.size(), 2u);

    // DynStrings
    auto *t3 = asDecl<TypeDecl>(prog->decls[2].get());
    EXPECT_EQ(t3->name, "DynStrings");
    auto *arr3 = asType<ArrayTypeNode>(t3->type.get());
    EXPECT_TRUE(arr3->dimensions.empty());
}

TEST(PascalParserDecl, TypeSectionOptional)
{
    auto prog = parseProg(
        "program Test;\n"
        "type\n"
        "  MaybeInt = Integer?;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->decls.size(), 1u);

    auto *td = asDecl<TypeDecl>(prog->decls[0].get());
    EXPECT_EQ(td->name, "MaybeInt");
    EXPECT_EQ(td->type->kind, TypeKind::Optional);
}

TEST(PascalParserDecl, TypeSectionRecord)
{
    auto prog = parseProg(
        "program Test;\n"
        "type\n"
        "  Point = record x, y: Real; end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->decls.size(), 1u);

    auto *td = asDecl<TypeDecl>(prog->decls[0].get());
    EXPECT_EQ(td->name, "Point");

    auto *rec = asType<RecordTypeNode>(td->type.get());
    EXPECT_TRUE(rec != nullptr);
    EXPECT_EQ(rec->fields.size(), 2u);
}

//===----------------------------------------------------------------------===//
// Var Section Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserDecl, VarSection)
{
    auto prog = parseProg(
        "program Test;\n"
        "var\n"
        "  x, y: Integer;\n"
        "  name: String;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->decls.size(), 2u);

    // First var decl has x, y
    auto *v1 = asDecl<VarDecl>(prog->decls[0].get());
    EXPECT_TRUE(v1 != nullptr);
    EXPECT_EQ(v1->names.size(), 2u);
    EXPECT_EQ(v1->names[0], "x");
    EXPECT_EQ(v1->names[1], "y");

    // Second var decl has name
    auto *v2 = asDecl<VarDecl>(prog->decls[1].get());
    EXPECT_TRUE(v2 != nullptr);
    EXPECT_EQ(v2->names.size(), 1u);
    EXPECT_EQ(v2->names[0], "name");
}

TEST(PascalParserDecl, VarWithInitializer)
{
    auto prog = parseProg(
        "program Test;\n"
        "var\n"
        "  count: Integer = 0;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->decls.size(), 1u);

    auto *v = asDecl<VarDecl>(prog->decls[0].get());
    EXPECT_TRUE(v != nullptr);
    EXPECT_EQ(v->names.size(), 1u);
    EXPECT_EQ(v->names[0], "count");
    EXPECT_TRUE(v->init != nullptr);
}

//===----------------------------------------------------------------------===//
// Procedure/Function Declaration Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserDecl, ProcedureSimple)
{
    auto prog = parseProg(
        "program Test;\n"
        "procedure DoNothing;\n"
        "begin end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->decls.size(), 1u);

    auto *proc = asDecl<ProcedureDecl>(prog->decls[0].get());
    EXPECT_TRUE(proc != nullptr);
    EXPECT_EQ(proc->name, "DoNothing");
    EXPECT_TRUE(proc->params.empty());
}

TEST(PascalParserDecl, ProcedureWithParams)
{
    auto prog = parseProg(
        "program Test;\n"
        "procedure PrintValue(x: Integer);\n"
        "begin end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->decls.size(), 1u);

    auto *proc = asDecl<ProcedureDecl>(prog->decls[0].get());
    EXPECT_TRUE(proc != nullptr);
    EXPECT_EQ(proc->name, "PrintValue");
    EXPECT_EQ(proc->params.size(), 1u);
    EXPECT_EQ(proc->params[0].name, "x");
}

TEST(PascalParserDecl, ProcedureWithVarParam)
{
    auto prog = parseProg(
        "program Test;\n"
        "procedure Swap(var a, b: Integer);\n"
        "begin end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);

    auto *proc = asDecl<ProcedureDecl>(prog->decls[0].get());
    EXPECT_TRUE(proc != nullptr);
    EXPECT_EQ(proc->params.size(), 2u);
    EXPECT_TRUE(proc->params[0].isVar);
    EXPECT_TRUE(proc->params[1].isVar);
}

TEST(PascalParserDecl, ProcedureWithConstParam)
{
    auto prog = parseProg(
        "program Test;\n"
        "procedure Process(const s: String);\n"
        "begin end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);

    auto *proc = asDecl<ProcedureDecl>(prog->decls[0].get());
    EXPECT_TRUE(proc != nullptr);
    EXPECT_EQ(proc->params.size(), 1u);
    EXPECT_TRUE(proc->params[0].isConst);
}

TEST(PascalParserDecl, FunctionSimple)
{
    auto prog = parseProg(
        "program Test;\n"
        "function GetValue: Integer;\n"
        "begin end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->decls.size(), 1u);

    auto *func = asDecl<FunctionDecl>(prog->decls[0].get());
    EXPECT_TRUE(func != nullptr);
    EXPECT_EQ(func->name, "GetValue");
    EXPECT_TRUE(func->returnType != nullptr);
}

TEST(PascalParserDecl, FunctionWithParams)
{
    auto prog = parseProg(
        "program Test;\n"
        "function Add(a, b: Integer): Integer;\n"
        "begin end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);

    auto *func = asDecl<FunctionDecl>(prog->decls[0].get());
    EXPECT_TRUE(func != nullptr);
    EXPECT_EQ(func->name, "Add");
    EXPECT_EQ(func->params.size(), 2u);
}

TEST(PascalParserDecl, FunctionWithDefaultParam)
{
    auto prog = parseProg(
        "program Test;\n"
        "function Greet(name: String = 'World'): String;\n"
        "begin end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);

    auto *func = asDecl<FunctionDecl>(prog->decls[0].get());
    EXPECT_TRUE(func != nullptr);
    EXPECT_EQ(func->params.size(), 1u);
    EXPECT_TRUE(func->params[0].defaultValue != nullptr);
}

TEST(PascalParserDecl, FunctionWithLocalVars)
{
    auto prog = parseProg(
        "program Test;\n"
        "function Square(x: Integer): Integer;\n"
        "var temp: Integer;\n"
        "begin end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);

    auto *func = asDecl<FunctionDecl>(prog->decls[0].get());
    EXPECT_TRUE(func != nullptr);
    EXPECT_EQ(func->localDecls.size(), 1u);
}

//===----------------------------------------------------------------------===//
// Class Declaration Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserDecl, ClassSimple)
{
    auto prog = parseProg(
        "program Test;\n"
        "type\n"
        "  TShape = class\n"
        "  end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->decls.size(), 1u);

    // Classes are stored directly as ClassDecl, not wrapped in TypeDecl
    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    EXPECT_TRUE(cls != nullptr);
    EXPECT_EQ(cls->name, "TShape");
    EXPECT_TRUE(cls->baseClass.empty());
    EXPECT_TRUE(cls->members.empty());
}

TEST(PascalParserDecl, ClassWithBaseClass)
{
    auto prog = parseProg(
        "program Test;\n"
        "type\n"
        "  TCircle = class(TShape)\n"
        "  end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->decls.size(), 1u);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    EXPECT_TRUE(cls != nullptr);
    EXPECT_EQ(cls->name, "TCircle");
    EXPECT_EQ(cls->baseClass, "TShape");
}

TEST(PascalParserDecl, ClassWithInterfaces)
{
    auto prog = parseProg(
        "program Test;\n"
        "type\n"
        "  TButton = class(TObject, IDrawable, IClickable)\n"
        "  end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    EXPECT_TRUE(cls != nullptr);
    EXPECT_EQ(cls->baseClass, "TObject");
    EXPECT_EQ(cls->interfaces.size(), 2u);
    EXPECT_EQ(cls->interfaces[0], "IDrawable");
    EXPECT_EQ(cls->interfaces[1], "IClickable");
}

TEST(PascalParserDecl, ClassWithFields)
{
    auto prog = parseProg(
        "program Test;\n"
        "type\n"
        "  TPoint = class\n"
        "    x: Real;\n"
        "    y: Real;\n"
        "  end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    EXPECT_TRUE(cls != nullptr);
    EXPECT_EQ(cls->members.size(), 2u);
    EXPECT_EQ(cls->members[0].memberKind, ClassMember::Kind::Field);
    EXPECT_EQ(cls->members[0].fieldName, "x");
    EXPECT_EQ(cls->members[1].fieldName, "y");
}

TEST(PascalParserDecl, ClassWithVisibility)
{
    auto prog = parseProg(
        "program Test;\n"
        "type\n"
        "  TCounter = class\n"
        "  private\n"
        "    count: Integer;\n"
        "  public\n"
        "    procedure Inc;\n"
        "    function GetCount: Integer;\n"
        "  end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    EXPECT_TRUE(cls != nullptr);
    EXPECT_EQ(cls->members.size(), 3u);

    // First member is private field
    EXPECT_EQ(cls->members[0].visibility, Visibility::Private);
    EXPECT_EQ(cls->members[0].memberKind, ClassMember::Kind::Field);

    // Second and third are public methods
    EXPECT_EQ(cls->members[1].visibility, Visibility::Public);
    EXPECT_EQ(cls->members[1].memberKind, ClassMember::Kind::Method);

    EXPECT_EQ(cls->members[2].visibility, Visibility::Public);
    EXPECT_EQ(cls->members[2].memberKind, ClassMember::Kind::Method);
}

TEST(PascalParserDecl, ClassWithConstructor)
{
    auto prog = parseProg(
        "program Test;\n"
        "type\n"
        "  TMyClass = class\n"
        "    constructor Create;\n"
        "    destructor Destroy;\n"
        "  end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    EXPECT_TRUE(cls != nullptr);
    EXPECT_EQ(cls->members.size(), 2u);
    EXPECT_EQ(cls->members[0].memberKind, ClassMember::Kind::Constructor);
    EXPECT_EQ(cls->members[1].memberKind, ClassMember::Kind::Destructor);
}

TEST(PascalParserDecl, ClassWithWeakField)
{
    auto prog = parseProg(
        "program Test;\n"
        "type\n"
        "  TNode = class\n"
        "    weak parent: TNode;\n"
        "    data: Integer;\n"
        "  end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);

    auto *cls = asDecl<ClassDecl>(prog->decls[0].get());
    EXPECT_TRUE(cls != nullptr);
    EXPECT_EQ(cls->members.size(), 2u);
    EXPECT_TRUE(cls->members[0].isWeak);
    EXPECT_FALSE(cls->members[1].isWeak);
}

//===----------------------------------------------------------------------===//
// Interface Declaration Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserDecl, InterfaceSimple)
{
    auto prog = parseProg(
        "program Test;\n"
        "type\n"
        "  IDrawable = interface\n"
        "    procedure Draw;\n"
        "  end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);

    auto *iface = asDecl<InterfaceDecl>(prog->decls[0].get());
    EXPECT_TRUE(iface != nullptr);
    EXPECT_EQ(iface->name, "IDrawable");
    EXPECT_EQ(iface->methods.size(), 1u);
    EXPECT_EQ(iface->methods[0].name, "Draw");
}

TEST(PascalParserDecl, InterfaceWithInheritance)
{
    auto prog = parseProg(
        "program Test;\n"
        "type\n"
        "  ISerializable = interface(IBase)\n"
        "    procedure Save;\n"
        "    procedure Load;\n"
        "  end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);

    auto *iface = asDecl<InterfaceDecl>(prog->decls[0].get());
    EXPECT_TRUE(iface != nullptr);
    EXPECT_EQ(iface->baseInterfaces.size(), 1u);
    EXPECT_EQ(iface->baseInterfaces[0], "IBase");
    EXPECT_EQ(iface->methods.size(), 2u);
}

TEST(PascalParserDecl, InterfaceWithFunction)
{
    auto prog = parseProg(
        "program Test;\n"
        "type\n"
        "  IComparable = interface\n"
        "    function Compare(other: IComparable): Integer;\n"
        "  end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);

    auto *iface = asDecl<InterfaceDecl>(prog->decls[0].get());
    EXPECT_TRUE(iface != nullptr);
    EXPECT_EQ(iface->methods.size(), 1u);
    EXPECT_EQ(iface->methods[0].name, "Compare");
    EXPECT_EQ(iface->methods[0].params.size(), 1u);
    EXPECT_TRUE(iface->methods[0].returnType != nullptr);
}

//===----------------------------------------------------------------------===//
// Unit Parsing Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserUnit, MinimalUnit)
{
    auto unit = parseUnitHelper(
        "unit MyUnit;\n"
        "interface\n"
        "implementation\n"
        "end.");
    EXPECT_TRUE(unit != nullptr);
    EXPECT_EQ(unit->name, "MyUnit");
    EXPECT_TRUE(unit->interfaceDecls.empty());
    EXPECT_TRUE(unit->implDecls.empty());
}

TEST(PascalParserUnit, UnitWithUses)
{
    auto unit = parseUnitHelper(
        "unit MyUnit;\n"
        "interface\n"
        "uses SysUtils, Classes;\n"
        "implementation\n"
        "end.");
    EXPECT_TRUE(unit != nullptr);
    EXPECT_EQ(unit->usedUnits.size(), 2u);
    EXPECT_EQ(unit->usedUnits[0], "SysUtils");
    EXPECT_EQ(unit->usedUnits[1], "Classes");
}

TEST(PascalParserUnit, UnitWithInterfaceConst)
{
    auto unit = parseUnitHelper(
        "unit MyMath;\n"
        "interface\n"
        "const\n"
        "  PI = 3.14159;\n"
        "implementation\n"
        "end.");
    EXPECT_TRUE(unit != nullptr);
    EXPECT_EQ(unit->interfaceDecls.size(), 1u);

    auto *c = asDecl<ConstDecl>(unit->interfaceDecls[0].get());
    EXPECT_TRUE(c != nullptr);
    EXPECT_EQ(c->name, "PI");
}

TEST(PascalParserUnit, UnitWithFunctionSignature)
{
    auto unit = parseUnitHelper(
        "unit MyMath;\n"
        "interface\n"
        "function Add(a, b: Integer): Integer;\n"
        "implementation\n"
        "function Add(a, b: Integer): Integer;\n"
        "begin end;\n"
        "end.");
    EXPECT_TRUE(unit != nullptr);
    EXPECT_EQ(unit->interfaceDecls.size(), 1u);
    EXPECT_EQ(unit->implDecls.size(), 1u);

    // Interface has forward declaration
    auto *fwdFunc = asDecl<FunctionDecl>(unit->interfaceDecls[0].get());
    EXPECT_TRUE(fwdFunc != nullptr);
    EXPECT_TRUE(fwdFunc->isForward);

    // Implementation has full body
    auto *implFunc = asDecl<FunctionDecl>(unit->implDecls[0].get());
    EXPECT_TRUE(implFunc != nullptr);
}

TEST(PascalParserUnit, UnitWithInitialization)
{
    auto unit = parseUnitHelper(
        "unit MyUnit;\n"
        "interface\n"
        "implementation\n"
        "initialization\n"
        "  WriteLn('Init')\n"
        "end.");
    EXPECT_TRUE(unit != nullptr);
    EXPECT_TRUE(unit->initSection != nullptr);
    EXPECT_TRUE(unit->finalSection == nullptr);
}

TEST(PascalParserUnit, UnitWithFinalization)
{
    auto unit = parseUnitHelper(
        "unit MyUnit;\n"
        "interface\n"
        "implementation\n"
        "initialization\n"
        "  WriteLn('Init')\n"
        "finalization\n"
        "  WriteLn('Cleanup')\n"
        "end.");
    EXPECT_TRUE(unit != nullptr);
    EXPECT_TRUE(unit->initSection != nullptr);
    EXPECT_TRUE(unit->finalSection != nullptr);
}

TEST(PascalParserUnit, CompleteUnit)
{
    auto unit = parseUnitHelper(
        "unit MyMath;\n"
        "interface\n"
        "uses SysUtils;\n"
        "const\n"
        "  PI = 3.14159;\n"
        "type\n"
        "  TOperation = (Add, Sub, Mul, Div);\n"
        "function Calculate(a, b: Real; op: TOperation): Real;\n"
        "implementation\n"
        "function Calculate(a, b: Real; op: TOperation): Real;\n"
        "begin end;\n"
        "end.");
    EXPECT_TRUE(unit != nullptr);
    EXPECT_EQ(unit->name, "MyMath");
    EXPECT_EQ(unit->usedUnits.size(), 1u);
    EXPECT_EQ(unit->interfaceDecls.size(), 3u); // const + type + function
    EXPECT_EQ(unit->implDecls.size(), 1u);
}

//===----------------------------------------------------------------------===//
// Parse Dispatch Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserDispatch, ParseProgram)
{
    DiagnosticEngine diag;
    Lexer lexer("program Hello; begin end.", 0, diag);
    Parser parser(lexer, diag);
    auto [prog, unit] = parser.parse();
    EXPECT_TRUE(prog != nullptr);
    EXPECT_TRUE(unit == nullptr);
}

TEST(PascalParserDispatch, ParseUnit)
{
    DiagnosticEngine diag;
    Lexer lexer("unit MyUnit; interface implementation end.", 0, diag);
    Parser parser(lexer, diag);
    auto [prog, unit] = parser.parse();
    EXPECT_TRUE(prog == nullptr);
    EXPECT_TRUE(unit != nullptr);
}

//===----------------------------------------------------------------------===//
// Comprehensive Integration Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserIntegration, TShapeTCircleExample)
{
    auto prog = parseProg(
        "program ShapeTest;\n"
        "type\n"
        "  TShape = class\n"
        "  private\n"
        "    x, y: Real;\n"
        "  public\n"
        "    constructor Create(ax, ay: Real);\n"
        "    procedure Draw; virtual;\n"
        "    function GetArea: Real; virtual;\n"
        "  end;\n"
        "\n"
        "  TCircle = class(TShape)\n"
        "  private\n"
        "    radius: Real;\n"
        "  public\n"
        "    constructor Create(ax, ay, r: Real);\n"
        "    procedure Draw; override;\n"
        "    function GetArea: Real; override;\n"
        "  end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->decls.size(), 2u);

    // TShape
    auto *shape = asDecl<ClassDecl>(prog->decls[0].get());
    EXPECT_TRUE(shape != nullptr);
    EXPECT_EQ(shape->name, "TShape");
    EXPECT_TRUE(shape->baseClass.empty());

    // TCircle
    auto *circle = asDecl<ClassDecl>(prog->decls[1].get());
    EXPECT_TRUE(circle != nullptr);
    EXPECT_EQ(circle->name, "TCircle");
    EXPECT_EQ(circle->baseClass, "TShape");
}

TEST(PascalParserIntegration, IDrawableImplementation)
{
    auto prog = parseProg(
        "program DrawTest;\n"
        "type\n"
        "  IDrawable = interface\n"
        "    procedure Draw;\n"
        "    function GetBounds: TRect;\n"
        "  end;\n"
        "\n"
        "  TButton = class(TControl, IDrawable)\n"
        "  private\n"
        "    caption: String;\n"
        "  public\n"
        "    procedure Draw;\n"
        "    function GetBounds: TRect;\n"
        "  end;\n"
        "begin end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->decls.size(), 2u);

    auto *iface = asDecl<InterfaceDecl>(prog->decls[0].get());
    EXPECT_TRUE(iface != nullptr);
    EXPECT_EQ(iface->name, "IDrawable");
    EXPECT_EQ(iface->methods.size(), 2u);

    auto *button = asDecl<ClassDecl>(prog->decls[1].get());
    EXPECT_TRUE(button != nullptr);
    EXPECT_EQ(button->baseClass, "TControl");
    EXPECT_EQ(button->interfaces.size(), 1u);
    EXPECT_EQ(button->interfaces[0], "IDrawable");
}

} // namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
