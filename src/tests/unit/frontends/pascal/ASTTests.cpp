//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/ASTTests.cpp
// Purpose: Unit tests for the Viper Pascal AST nodes.
// Key invariants: Verifies node construction, ownership, and structure.
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
#include <memory>
#include <string>
#include <vector>

using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

//===----------------------------------------------------------------------===//
// Expression Node Tests
//===----------------------------------------------------------------------===//

TEST(PascalASTTest, IntLiteralExpr)
{
    IntLiteralExpr expr(42, SourceLoc{1, 1, 1});
    EXPECT_EQ(expr.kind, ExprKind::IntLiteral);
    EXPECT_EQ(expr.value, 42);
    EXPECT_EQ(expr.loc.line, 1u);
}

TEST(PascalASTTest, RealLiteralExpr)
{
    RealLiteralExpr expr(3.14, SourceLoc{1, 2, 1});
    EXPECT_EQ(expr.kind, ExprKind::RealLiteral);
    EXPECT_TRUE(expr.value > 3.13 && expr.value < 3.15);
}

TEST(PascalASTTest, StringLiteralExpr)
{
    StringLiteralExpr expr("Hello, World!", SourceLoc{1, 3, 1});
    EXPECT_EQ(expr.kind, ExprKind::StringLiteral);
    EXPECT_EQ(expr.value, "Hello, World!");
}

TEST(PascalASTTest, BoolLiteralExpr)
{
    BoolLiteralExpr trueExpr(true);
    BoolLiteralExpr falseExpr(false);
    EXPECT_EQ(trueExpr.kind, ExprKind::BoolLiteral);
    EXPECT_TRUE(trueExpr.value);
    EXPECT_FALSE(falseExpr.value);
}

TEST(PascalASTTest, NilLiteralExpr)
{
    NilLiteralExpr expr;
    EXPECT_EQ(expr.kind, ExprKind::NilLiteral);
}

TEST(PascalASTTest, NameExpr)
{
    NameExpr expr("MyVariable");
    EXPECT_EQ(expr.kind, ExprKind::Name);
    EXPECT_EQ(expr.name, "MyVariable");
}

TEST(PascalASTTest, UnaryExpr)
{
    auto operand = std::make_unique<IntLiteralExpr>(5);
    UnaryExpr expr(UnaryExpr::Op::Neg, std::move(operand));

    EXPECT_EQ(expr.kind, ExprKind::Unary);
    EXPECT_EQ(expr.op, UnaryExpr::Op::Neg);
    EXPECT_TRUE(expr.operand != nullptr);

    auto *intExpr = dynamic_cast<IntLiteralExpr *>(expr.operand.get());
    EXPECT_TRUE(intExpr != nullptr);
    EXPECT_EQ(intExpr->value, 5);
}

TEST(PascalASTTest, BinaryExpr)
{
    auto left = std::make_unique<IntLiteralExpr>(10);
    auto right = std::make_unique<IntLiteralExpr>(20);
    BinaryExpr expr(BinaryExpr::Op::Add, std::move(left), std::move(right));

    EXPECT_EQ(expr.kind, ExprKind::Binary);
    EXPECT_EQ(expr.op, BinaryExpr::Op::Add);
    EXPECT_TRUE(expr.left != nullptr);
    EXPECT_TRUE(expr.right != nullptr);

    auto *leftInt = dynamic_cast<IntLiteralExpr *>(expr.left.get());
    auto *rightInt = dynamic_cast<IntLiteralExpr *>(expr.right.get());
    EXPECT_EQ(leftInt->value, 10);
    EXPECT_EQ(rightInt->value, 20);
}

TEST(PascalASTTest, CallExpr)
{
    auto callee = std::make_unique<NameExpr>("WriteLn");
    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<StringLiteralExpr>("Hello"));
    args.push_back(std::make_unique<IntLiteralExpr>(42));

    CallExpr expr(std::move(callee), std::move(args));

    EXPECT_EQ(expr.kind, ExprKind::Call);
    EXPECT_EQ(expr.args.size(), 2u);

    auto *nameExpr = dynamic_cast<NameExpr *>(expr.callee.get());
    EXPECT_EQ(nameExpr->name, "WriteLn");
}

TEST(PascalASTTest, IndexExpr)
{
    auto base = std::make_unique<NameExpr>("arr");
    std::vector<std::unique_ptr<Expr>> indices;
    indices.push_back(std::make_unique<IntLiteralExpr>(0));
    indices.push_back(std::make_unique<IntLiteralExpr>(1));

    IndexExpr expr(std::move(base), std::move(indices));

    EXPECT_EQ(expr.kind, ExprKind::Index);
    EXPECT_EQ(expr.indices.size(), 2u);
}

TEST(PascalASTTest, FieldExpr)
{
    auto base = std::make_unique<NameExpr>("obj");
    FieldExpr expr(std::move(base), "fieldName");

    EXPECT_EQ(expr.kind, ExprKind::Field);
    EXPECT_EQ(expr.field, "fieldName");
}

//===----------------------------------------------------------------------===//
// Type Node Tests
//===----------------------------------------------------------------------===//

TEST(PascalASTTest, NamedTypeNode)
{
    NamedTypeNode type("Integer");
    EXPECT_EQ(type.kind, TypeKind::Named);
    EXPECT_EQ(type.name, "Integer");
}

TEST(PascalASTTest, OptionalTypeNode)
{
    auto inner = std::make_unique<NamedTypeNode>("String");
    OptionalTypeNode type(std::move(inner));

    EXPECT_EQ(type.kind, TypeKind::Optional);
    EXPECT_TRUE(type.inner != nullptr);

    auto *namedType = dynamic_cast<NamedTypeNode *>(type.inner.get());
    EXPECT_EQ(namedType->name, "String");
}

TEST(PascalASTTest, ArrayTypeNode)
{
    // Arrays are 0-based in v0.1, so we only store the size
    std::vector<ArrayTypeNode::DimSize> dims;
    ArrayTypeNode::DimSize dim;
    dim.size = std::make_unique<IntLiteralExpr>(10);
    dims.push_back(std::move(dim));

    auto elemType = std::make_unique<NamedTypeNode>("Integer");
    ArrayTypeNode type(std::move(dims), std::move(elemType));

    EXPECT_EQ(type.kind, TypeKind::Array);
    EXPECT_EQ(type.dimensions.size(), 1u);
    EXPECT_TRUE(type.elementType != nullptr);
}

TEST(PascalASTTest, DynamicArrayTypeNode)
{
    std::vector<ArrayTypeNode::DimSize> dims; // Empty for dynamic
    auto elemType = std::make_unique<NamedTypeNode>("String");
    ArrayTypeNode type(std::move(dims), std::move(elemType));

    EXPECT_EQ(type.kind, TypeKind::Array);
    EXPECT_TRUE(type.dimensions.empty());
}

TEST(PascalASTTest, RecordTypeNode)
{
    std::vector<RecordField> fields;

    RecordField field1;
    field1.name = "x";
    field1.type = std::make_unique<NamedTypeNode>("Integer");
    fields.push_back(std::move(field1));

    RecordField field2;
    field2.name = "y";
    field2.type = std::make_unique<NamedTypeNode>("Integer");
    fields.push_back(std::move(field2));

    RecordTypeNode type(std::move(fields));

    EXPECT_EQ(type.kind, TypeKind::Record);
    EXPECT_EQ(type.fields.size(), 2u);
    EXPECT_EQ(type.fields[0].name, "x");
    EXPECT_EQ(type.fields[1].name, "y");
}

TEST(PascalASTTest, PointerTypeNode)
{
    auto pointee = std::make_unique<NamedTypeNode>("TRecord");
    PointerTypeNode type(std::move(pointee));

    EXPECT_EQ(type.kind, TypeKind::Pointer);
    EXPECT_TRUE(type.pointeeType != nullptr);
}

//===----------------------------------------------------------------------===//
// Statement Node Tests
//===----------------------------------------------------------------------===//

TEST(PascalASTTest, AssignStmt)
{
    auto target = std::make_unique<NameExpr>("x");
    auto value = std::make_unique<IntLiteralExpr>(42);
    AssignStmt stmt(std::move(target), std::move(value));

    EXPECT_EQ(stmt.kind, StmtKind::Assign);
    EXPECT_TRUE(stmt.target != nullptr);
    EXPECT_TRUE(stmt.value != nullptr);
}

TEST(PascalASTTest, BlockStmt)
{
    std::vector<std::unique_ptr<Stmt>> stmts;
    stmts.push_back(std::make_unique<EmptyStmt>());
    stmts.push_back(std::make_unique<BreakStmt>());

    BlockStmt block(std::move(stmts));

    EXPECT_EQ(block.kind, StmtKind::Block);
    EXPECT_EQ(block.stmts.size(), 2u);
}

TEST(PascalASTTest, IfStmt)
{
    auto cond = std::make_unique<BoolLiteralExpr>(true);
    auto thenBr = std::make_unique<EmptyStmt>();
    auto elseBr = std::make_unique<EmptyStmt>();

    IfStmt stmt(std::move(cond), std::move(thenBr), std::move(elseBr));

    EXPECT_EQ(stmt.kind, StmtKind::If);
    EXPECT_TRUE(stmt.condition != nullptr);
    EXPECT_TRUE(stmt.thenBranch != nullptr);
    EXPECT_TRUE(stmt.elseBranch != nullptr);
}

TEST(PascalASTTest, IfStmtNoElse)
{
    auto cond = std::make_unique<BoolLiteralExpr>(true);
    auto thenBr = std::make_unique<EmptyStmt>();

    IfStmt stmt(std::move(cond), std::move(thenBr));

    EXPECT_EQ(stmt.kind, StmtKind::If);
    EXPECT_TRUE(stmt.elseBranch == nullptr);
}

TEST(PascalASTTest, ForStmt)
{
    auto start = std::make_unique<IntLiteralExpr>(1);
    auto bound = std::make_unique<IntLiteralExpr>(10);
    auto body = std::make_unique<EmptyStmt>();

    ForStmt stmt("i", std::move(start), std::move(bound), ForDirection::To, std::move(body));

    EXPECT_EQ(stmt.kind, StmtKind::For);
    EXPECT_EQ(stmt.loopVar, "i");
    EXPECT_EQ(stmt.direction, ForDirection::To);
    EXPECT_TRUE(stmt.start != nullptr);
    EXPECT_TRUE(stmt.bound != nullptr);
    EXPECT_TRUE(stmt.body != nullptr);
}

TEST(PascalASTTest, ForStmtDownto)
{
    auto start = std::make_unique<IntLiteralExpr>(10);
    auto bound = std::make_unique<IntLiteralExpr>(1);
    auto body = std::make_unique<EmptyStmt>();

    ForStmt stmt("i", std::move(start), std::move(bound), ForDirection::Downto, std::move(body));

    EXPECT_EQ(stmt.direction, ForDirection::Downto);
}

TEST(PascalASTTest, ForInStmt)
{
    auto collection = std::make_unique<NameExpr>("items");
    auto body = std::make_unique<EmptyStmt>();

    ForInStmt stmt("item", std::move(collection), std::move(body));

    EXPECT_EQ(stmt.kind, StmtKind::ForIn);
    EXPECT_EQ(stmt.loopVar, "item");
    EXPECT_TRUE(stmt.collection != nullptr);
    EXPECT_TRUE(stmt.body != nullptr);
}

TEST(PascalASTTest, WhileStmt)
{
    auto cond = std::make_unique<BoolLiteralExpr>(true);
    auto body = std::make_unique<EmptyStmt>();

    WhileStmt stmt(std::move(cond), std::move(body));

    EXPECT_EQ(stmt.kind, StmtKind::While);
}

TEST(PascalASTTest, RepeatStmt)
{
    auto body = std::make_unique<EmptyStmt>();
    auto cond = std::make_unique<BoolLiteralExpr>(false);

    RepeatStmt stmt(std::move(body), std::move(cond));

    EXPECT_EQ(stmt.kind, StmtKind::Repeat);
}

TEST(PascalASTTest, TryFinallyStmt)
{
    auto tryBody = std::make_unique<BlockStmt>();
    auto finallyBody = std::make_unique<BlockStmt>();

    TryFinallyStmt stmt(std::move(tryBody), std::move(finallyBody));

    EXPECT_EQ(stmt.kind, StmtKind::TryFinally);
    EXPECT_TRUE(stmt.tryBody != nullptr);
    EXPECT_TRUE(stmt.finallyBody != nullptr);
}

TEST(PascalASTTest, TryExceptStmt)
{
    auto tryBody = std::make_unique<BlockStmt>();

    std::vector<ExceptHandler> handlers;
    ExceptHandler handler;
    handler.varName = "E";
    handler.typeName = "Exception";
    handler.body = std::make_unique<EmptyStmt>();
    handlers.push_back(std::move(handler));

    TryExceptStmt stmt(std::move(tryBody), std::move(handlers));

    EXPECT_EQ(stmt.kind, StmtKind::TryExcept);
    EXPECT_EQ(stmt.handlers.size(), 1u);
    EXPECT_EQ(stmt.handlers[0].varName, "E");
    EXPECT_EQ(stmt.handlers[0].typeName, "Exception");
}

//===----------------------------------------------------------------------===//
// Declaration Node Tests
//===----------------------------------------------------------------------===//

TEST(PascalASTTest, ConstDecl)
{
    auto value = std::make_unique<IntLiteralExpr>(100);
    ConstDecl decl("MAX_VALUE", std::move(value));

    EXPECT_EQ(decl.kind, DeclKind::Const);
    EXPECT_EQ(decl.name, "MAX_VALUE");
    EXPECT_TRUE(decl.value != nullptr);
    EXPECT_TRUE(decl.type == nullptr); // Type inferred
}

TEST(PascalASTTest, ConstDeclWithType)
{
    auto value = std::make_unique<IntLiteralExpr>(100);
    auto type = std::make_unique<NamedTypeNode>("Integer");
    ConstDecl decl("MAX_VALUE", std::move(value), std::move(type));

    EXPECT_TRUE(decl.type != nullptr);
}

TEST(PascalASTTest, VarDecl)
{
    std::vector<std::string> names = {"x", "y", "z"};
    auto type = std::make_unique<NamedTypeNode>("Integer");
    VarDecl decl(names, std::move(type));

    EXPECT_EQ(decl.kind, DeclKind::Var);
    EXPECT_EQ(decl.names.size(), 3u);
    EXPECT_EQ(decl.names[0], "x");
    EXPECT_EQ(decl.names[1], "y");
    EXPECT_EQ(decl.names[2], "z");
    EXPECT_TRUE(decl.init == nullptr);
}

TEST(PascalASTTest, VarDeclWithInit)
{
    std::vector<std::string> names = {"counter"};
    auto type = std::make_unique<NamedTypeNode>("Integer");
    auto init = std::make_unique<IntLiteralExpr>(0);
    VarDecl decl(names, std::move(type), std::move(init));

    EXPECT_TRUE(decl.init != nullptr);
}

TEST(PascalASTTest, TypeDecl)
{
    auto type = std::make_unique<NamedTypeNode>("Integer");
    TypeDecl decl("TMyInt", std::move(type));

    EXPECT_EQ(decl.kind, DeclKind::Type);
    EXPECT_EQ(decl.name, "TMyInt");
}

TEST(PascalASTTest, ProcedureDecl)
{
    std::vector<ParamDecl> params;
    ParamDecl param;
    param.name = "x";
    param.type = std::make_unique<NamedTypeNode>("Integer");
    param.isVar = false;
    params.push_back(std::move(param));

    ProcedureDecl decl("DoSomething", std::move(params));

    EXPECT_EQ(decl.kind, DeclKind::Procedure);
    EXPECT_EQ(decl.name, "DoSomething");
    EXPECT_EQ(decl.params.size(), 1u);
    EXPECT_EQ(decl.params[0].name, "x");
}

TEST(PascalASTTest, FunctionDecl)
{
    std::vector<ParamDecl> params;
    ParamDecl param;
    param.name = "n";
    param.type = std::make_unique<NamedTypeNode>("Integer");
    params.push_back(std::move(param));

    auto returnType = std::make_unique<NamedTypeNode>("Integer");
    FunctionDecl decl("Factorial", std::move(params), std::move(returnType));

    EXPECT_EQ(decl.kind, DeclKind::Function);
    EXPECT_EQ(decl.name, "Factorial");
    EXPECT_TRUE(decl.returnType != nullptr);
}

TEST(PascalASTTest, ClassDecl)
{
    ClassDecl decl("TMyClass");
    decl.baseClass = "TObject";
    decl.interfaces = {"IComparable", "ICloneable"};

    // Add private field
    ClassMember field;
    field.visibility = Visibility::Private;
    field.memberKind = ClassMember::Kind::Field;
    field.fieldName = "FValue";
    field.fieldType = std::make_unique<NamedTypeNode>("Integer");
    decl.members.push_back(std::move(field));

    // Add public method
    ClassMember method;
    method.visibility = Visibility::Public;
    method.memberKind = ClassMember::Kind::Method;
    std::vector<ParamDecl> params;
    method.methodDecl = std::make_unique<FunctionDecl>(
        "GetValue", std::move(params), std::make_unique<NamedTypeNode>("Integer"));
    decl.members.push_back(std::move(method));

    EXPECT_EQ(decl.kind, DeclKind::Class);
    EXPECT_EQ(decl.name, "TMyClass");
    EXPECT_EQ(decl.baseClass, "TObject");
    EXPECT_EQ(decl.interfaces.size(), 2u);
    EXPECT_EQ(decl.members.size(), 2u);

    // Check private field
    EXPECT_EQ(decl.members[0].visibility, Visibility::Private);
    EXPECT_EQ(decl.members[0].memberKind, ClassMember::Kind::Field);
    EXPECT_EQ(decl.members[0].fieldName, "FValue");

    // Check public method
    EXPECT_EQ(decl.members[1].visibility, Visibility::Public);
    EXPECT_EQ(decl.members[1].memberKind, ClassMember::Kind::Method);
}

TEST(PascalASTTest, InterfaceDecl)
{
    InterfaceDecl decl("IComparable");
    decl.baseInterfaces = {"IEquatable"};

    MethodSig sig;
    sig.name = "CompareTo";
    sig.returnType = std::make_unique<NamedTypeNode>("Integer");
    ParamDecl param;
    param.name = "other";
    param.type = std::make_unique<NamedTypeNode>("TObject");
    sig.params.push_back(std::move(param));
    decl.methods.push_back(std::move(sig));

    EXPECT_EQ(decl.kind, DeclKind::Interface);
    EXPECT_EQ(decl.name, "IComparable");
    EXPECT_EQ(decl.methods.size(), 1u);
    EXPECT_EQ(decl.methods[0].name, "CompareTo");
}

TEST(PascalASTTest, ConstructorDecl)
{
    std::vector<ParamDecl> params;
    ConstructorDecl decl("Create", std::move(params));

    EXPECT_EQ(decl.kind, DeclKind::Constructor);
    EXPECT_EQ(decl.name, "Create");
}

TEST(PascalASTTest, DestructorDecl)
{
    DestructorDecl decl("Destroy");

    EXPECT_EQ(decl.kind, DeclKind::Destructor);
    EXPECT_EQ(decl.name, "Destroy");
}

//===----------------------------------------------------------------------===//
// Top-Level Structure Tests
//===----------------------------------------------------------------------===//

TEST(PascalASTTest, ProgramWithVarDeclAndCallStmt)
{
    Program prog;
    prog.name = "Hello";
    prog.loc = SourceLoc{1, 1, 1};

    // Add var declaration: var x: Integer;
    std::vector<std::string> names = {"x"};
    auto type = std::make_unique<NamedTypeNode>("Integer");
    prog.decls.push_back(std::make_unique<VarDecl>(names, std::move(type)));

    // Add body with call statement: WriteLn('Hello');
    std::vector<std::unique_ptr<Stmt>> stmts;
    auto callee = std::make_unique<NameExpr>("WriteLn");
    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<StringLiteralExpr>("Hello"));
    auto call = std::make_unique<CallExpr>(std::move(callee), std::move(args));
    stmts.push_back(std::make_unique<CallStmt>(std::move(call)));

    prog.body = std::make_unique<BlockStmt>(std::move(stmts));

    EXPECT_EQ(prog.name, "Hello");
    EXPECT_EQ(prog.decls.size(), 1u);
    EXPECT_TRUE(prog.body != nullptr);
    EXPECT_EQ(prog.body->stmts.size(), 1u);

    // Verify ownership
    auto *varDecl = dynamic_cast<VarDecl *>(prog.decls[0].get());
    EXPECT_TRUE(varDecl != nullptr);
    EXPECT_EQ(varDecl->names[0], "x");
}

TEST(PascalASTTest, Unit)
{
    Unit unit;
    unit.name = "MyUnit";
    unit.usedUnits = {"SysUtils", "Classes"};

    // Interface declaration
    std::vector<ParamDecl> params;
    auto retType = std::make_unique<NamedTypeNode>("Integer");
    unit.interfaceDecls.push_back(
        std::make_unique<FunctionDecl>("MyFunc", std::move(params), std::move(retType)));

    // Implementation uses
    unit.implUsedUnits = {"StrUtils"};

    EXPECT_EQ(unit.name, "MyUnit");
    EXPECT_EQ(unit.usedUnits.size(), 2u);
    EXPECT_EQ(unit.interfaceDecls.size(), 1u);
    EXPECT_EQ(unit.implUsedUnits.size(), 1u);
}

//===----------------------------------------------------------------------===//
// Helper Function Tests
//===----------------------------------------------------------------------===//

TEST(PascalASTTest, ExprKindToString)
{
    EXPECT_EQ(std::string(exprKindToString(ExprKind::IntLiteral)), "IntLiteral");
    EXPECT_EQ(std::string(exprKindToString(ExprKind::Binary)), "Binary");
    EXPECT_EQ(std::string(exprKindToString(ExprKind::Call)), "Call");
}

TEST(PascalASTTest, StmtKindToString)
{
    EXPECT_EQ(std::string(stmtKindToString(StmtKind::Assign)), "Assign");
    EXPECT_EQ(std::string(stmtKindToString(StmtKind::For)), "For");
    EXPECT_EQ(std::string(stmtKindToString(StmtKind::TryFinally)), "TryFinally");
}

TEST(PascalASTTest, DeclKindToString)
{
    EXPECT_EQ(std::string(declKindToString(DeclKind::Var)), "Var");
    EXPECT_EQ(std::string(declKindToString(DeclKind::Class)), "Class");
    EXPECT_EQ(std::string(declKindToString(DeclKind::Function)), "Function");
}

TEST(PascalASTTest, TypeKindToString)
{
    EXPECT_EQ(std::string(typeKindToString(TypeKind::Named)), "Named");
    EXPECT_EQ(std::string(typeKindToString(TypeKind::Optional)), "Optional");
    EXPECT_EQ(std::string(typeKindToString(TypeKind::Array)), "Array");
}

} // namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
