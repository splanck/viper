//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/ParserExprStmtTests.cpp
// Purpose: Unit tests for the Viper Pascal parser (expressions and statements).
// Key invariants: Verifies precedence climbing and AST structure.
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

/// @brief Helper to parse an expression from source text.
std::unique_ptr<Expr> parseExpr(const std::string &source)
{
    DiagnosticEngine diag;
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    return parser.parseExpression();
}

/// @brief Helper to parse a statement from source text.
std::unique_ptr<Stmt> parseStmt(const std::string &source)
{
    DiagnosticEngine diag;
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    return parser.parseStatement();
}

/// @brief Helper to parse a program from source text.
std::unique_ptr<Program> parseProg(const std::string &source)
{
    DiagnosticEngine diag;
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    return parser.parseProgram();
}

/// @brief Cast expression to specific type.
template <typename T> T *asExpr(Expr *e)
{
    return dynamic_cast<T *>(e);
}

/// @brief Cast statement to specific type.
template <typename T> T *asStmt(Stmt *s)
{
    return dynamic_cast<T *>(s);
}

//===----------------------------------------------------------------------===//
// Expression Precedence Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserExpr, AdditionMultiplicationPrecedence)
{
    // "1 + 2 * 3" should parse as "1 + (2 * 3)"
    auto expr = parseExpr("1 + 2 * 3");
    EXPECT_TRUE(expr != nullptr);

    auto *binary = asExpr<BinaryExpr>(expr.get());
    EXPECT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->op, BinaryExpr::Op::Add);

    // Left is 1
    auto *left = asExpr<IntLiteralExpr>(binary->left.get());
    EXPECT_TRUE(left != nullptr);
    EXPECT_EQ(left->value, 1);

    // Right is 2 * 3
    auto *right = asExpr<BinaryExpr>(binary->right.get());
    EXPECT_TRUE(right != nullptr);
    EXPECT_EQ(right->op, BinaryExpr::Op::Mul);

    auto *rightLeft = asExpr<IntLiteralExpr>(right->left.get());
    auto *rightRight = asExpr<IntLiteralExpr>(right->right.get());
    EXPECT_EQ(rightLeft->value, 2);
    EXPECT_EQ(rightRight->value, 3);
}

TEST(PascalParserExpr, CoalesceLeftAssociative)
{
    // "a ?? b ?? c" should parse as "(a ?? b) ?? c"
    auto expr = parseExpr("a ?? b ?? c");
    EXPECT_TRUE(expr != nullptr);

    auto *outer = asExpr<BinaryExpr>(expr.get());
    EXPECT_TRUE(outer != nullptr);
    EXPECT_EQ(outer->op, BinaryExpr::Op::Coalesce);

    // Right is c
    auto *right = asExpr<NameExpr>(outer->right.get());
    EXPECT_TRUE(right != nullptr);
    EXPECT_EQ(right->name, "c");

    // Left is a ?? b
    auto *inner = asExpr<BinaryExpr>(outer->left.get());
    EXPECT_TRUE(inner != nullptr);
    EXPECT_EQ(inner->op, BinaryExpr::Op::Coalesce);

    auto *a = asExpr<NameExpr>(inner->left.get());
    auto *b = asExpr<NameExpr>(inner->right.get());
    EXPECT_EQ(a->name, "a");
    EXPECT_EQ(b->name, "b");
}

TEST(PascalParserExpr, NotAndPrecedence)
{
    // "not a and b" should parse as "(not a) and b"
    auto expr = parseExpr("not a and b");
    EXPECT_TRUE(expr != nullptr);

    auto *binary = asExpr<BinaryExpr>(expr.get());
    EXPECT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->op, BinaryExpr::Op::And);

    // Left is (not a)
    auto *left = asExpr<UnaryExpr>(binary->left.get());
    EXPECT_TRUE(left != nullptr);
    EXPECT_EQ(left->op, UnaryExpr::Op::Not);

    auto *a = asExpr<NameExpr>(left->operand.get());
    EXPECT_EQ(a->name, "a");

    // Right is b
    auto *right = asExpr<NameExpr>(binary->right.get());
    EXPECT_EQ(right->name, "b");
}

TEST(PascalParserExpr, UnaryMinusMultiplyPrecedence)
{
    // "-x * y" should parse as "(-x) * y"
    auto expr = parseExpr("-x * y");
    EXPECT_TRUE(expr != nullptr);

    auto *binary = asExpr<BinaryExpr>(expr.get());
    EXPECT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->op, BinaryExpr::Op::Mul);

    // Left is (-x)
    auto *left = asExpr<UnaryExpr>(binary->left.get());
    EXPECT_TRUE(left != nullptr);
    EXPECT_EQ(left->op, UnaryExpr::Op::Neg);

    auto *x = asExpr<NameExpr>(left->operand.get());
    EXPECT_EQ(x->name, "x");

    // Right is y
    auto *right = asExpr<NameExpr>(binary->right.get());
    EXPECT_EQ(right->name, "y");
}

TEST(PascalParserExpr, RelationalWithArithmetic)
{
    // "a + b < c * d" should parse as "(a + b) < (c * d)"
    auto expr = parseExpr("a + b < c * d");
    EXPECT_TRUE(expr != nullptr);

    auto *binary = asExpr<BinaryExpr>(expr.get());
    EXPECT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->op, BinaryExpr::Op::Lt);

    // Left is a + b
    auto *left = asExpr<BinaryExpr>(binary->left.get());
    EXPECT_TRUE(left != nullptr);
    EXPECT_EQ(left->op, BinaryExpr::Op::Add);

    // Right is c * d
    auto *right = asExpr<BinaryExpr>(binary->right.get());
    EXPECT_TRUE(right != nullptr);
    EXPECT_EQ(right->op, BinaryExpr::Op::Mul);
}

TEST(PascalParserExpr, DivModPrecedence)
{
    // "a div b mod c" should parse as "(a div b) mod c"
    auto expr = parseExpr("a div b mod c");
    EXPECT_TRUE(expr != nullptr);

    auto *outer = asExpr<BinaryExpr>(expr.get());
    EXPECT_TRUE(outer != nullptr);
    EXPECT_EQ(outer->op, BinaryExpr::Op::Mod);

    auto *inner = asExpr<BinaryExpr>(outer->left.get());
    EXPECT_TRUE(inner != nullptr);
    EXPECT_EQ(inner->op, BinaryExpr::Op::IntDiv);
}

TEST(PascalParserExpr, OrAdditivePrecedence)
{
    // "a or b - c" should parse as "(a or b) - c"
    auto expr = parseExpr("a or b - c");
    EXPECT_TRUE(expr != nullptr);

    auto *outer = asExpr<BinaryExpr>(expr.get());
    EXPECT_TRUE(outer != nullptr);
    EXPECT_EQ(outer->op, BinaryExpr::Op::Sub);

    auto *inner = asExpr<BinaryExpr>(outer->left.get());
    EXPECT_TRUE(inner != nullptr);
    EXPECT_EQ(inner->op, BinaryExpr::Op::Or);
}

//===----------------------------------------------------------------------===//
// Expression Literal Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserExpr, IntegerLiteral)
{
    auto expr = parseExpr("42");
    EXPECT_TRUE(expr != nullptr);

    auto *lit = asExpr<IntLiteralExpr>(expr.get());
    EXPECT_TRUE(lit != nullptr);
    EXPECT_EQ(lit->value, 42);
}

TEST(PascalParserExpr, RealLiteral)
{
    auto expr = parseExpr("3.14");
    EXPECT_TRUE(expr != nullptr);

    auto *lit = asExpr<RealLiteralExpr>(expr.get());
    EXPECT_TRUE(lit != nullptr);
    EXPECT_TRUE(lit->value > 3.13 && lit->value < 3.15);
}

TEST(PascalParserExpr, StringLiteral)
{
    auto expr = parseExpr("'Hello'");
    EXPECT_TRUE(expr != nullptr);

    auto *lit = asExpr<StringLiteralExpr>(expr.get());
    EXPECT_TRUE(lit != nullptr);
    EXPECT_EQ(lit->value, "Hello");
}

TEST(PascalParserExpr, BoolLiteralTrue)
{
    auto expr = parseExpr("True");
    EXPECT_TRUE(expr != nullptr);

    auto *lit = asExpr<BoolLiteralExpr>(expr.get());
    EXPECT_TRUE(lit != nullptr);
    EXPECT_TRUE(lit->value);
}

TEST(PascalParserExpr, BoolLiteralFalse)
{
    auto expr = parseExpr("False");
    EXPECT_TRUE(expr != nullptr);

    auto *lit = asExpr<BoolLiteralExpr>(expr.get());
    EXPECT_TRUE(lit != nullptr);
    EXPECT_FALSE(lit->value);
}

TEST(PascalParserExpr, NilLiteral)
{
    auto expr = parseExpr("nil");
    EXPECT_TRUE(expr != nullptr);

    auto *lit = asExpr<NilLiteralExpr>(expr.get());
    EXPECT_TRUE(lit != nullptr);
}

//===----------------------------------------------------------------------===//
// Designator Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserExpr, SimpleIdentifier)
{
    auto expr = parseExpr("myVar");
    EXPECT_TRUE(expr != nullptr);

    auto *name = asExpr<NameExpr>(expr.get());
    EXPECT_TRUE(name != nullptr);
    EXPECT_EQ(name->name, "myVar");
}

TEST(PascalParserExpr, FieldAccess)
{
    auto expr = parseExpr("obj.field");
    EXPECT_TRUE(expr != nullptr);

    auto *field = asExpr<FieldExpr>(expr.get());
    EXPECT_TRUE(field != nullptr);
    EXPECT_EQ(field->field, "field");

    auto *base = asExpr<NameExpr>(field->base.get());
    EXPECT_TRUE(base != nullptr);
    EXPECT_EQ(base->name, "obj");
}

TEST(PascalParserExpr, ChainedFieldAccess)
{
    auto expr = parseExpr("a.b.c");
    EXPECT_TRUE(expr != nullptr);

    auto *outer = asExpr<FieldExpr>(expr.get());
    EXPECT_TRUE(outer != nullptr);
    EXPECT_EQ(outer->field, "c");

    auto *inner = asExpr<FieldExpr>(outer->base.get());
    EXPECT_TRUE(inner != nullptr);
    EXPECT_EQ(inner->field, "b");

    auto *base = asExpr<NameExpr>(inner->base.get());
    EXPECT_TRUE(base != nullptr);
    EXPECT_EQ(base->name, "a");
}

TEST(PascalParserExpr, ArrayIndex)
{
    auto expr = parseExpr("arr[0]");
    EXPECT_TRUE(expr != nullptr);

    auto *index = asExpr<IndexExpr>(expr.get());
    EXPECT_TRUE(index != nullptr);
    EXPECT_EQ(index->indices.size(), 1u);

    auto *base = asExpr<NameExpr>(index->base.get());
    EXPECT_EQ(base->name, "arr");

    auto *idx = asExpr<IntLiteralExpr>(index->indices[0].get());
    EXPECT_EQ(idx->value, 0);
}

TEST(PascalParserExpr, MultiDimArrayIndex)
{
    auto expr = parseExpr("matrix[i, j]");
    EXPECT_TRUE(expr != nullptr);

    auto *index = asExpr<IndexExpr>(expr.get());
    EXPECT_TRUE(index != nullptr);
    EXPECT_EQ(index->indices.size(), 2u);
}

TEST(PascalParserExpr, FunctionCall)
{
    auto expr = parseExpr("func(1, 2)");
    EXPECT_TRUE(expr != nullptr);

    auto *call = asExpr<CallExpr>(expr.get());
    EXPECT_TRUE(call != nullptr);
    EXPECT_EQ(call->args.size(), 2u);

    auto *callee = asExpr<NameExpr>(call->callee.get());
    EXPECT_EQ(callee->name, "func");
}

TEST(PascalParserExpr, FunctionCallNoArgs)
{
    auto expr = parseExpr("func()");
    EXPECT_TRUE(expr != nullptr);

    auto *call = asExpr<CallExpr>(expr.get());
    EXPECT_TRUE(call != nullptr);
    EXPECT_EQ(call->args.size(), 0u);
}

TEST(PascalParserExpr, MethodCall)
{
    auto expr = parseExpr("obj.method(x)");
    EXPECT_TRUE(expr != nullptr);

    auto *call = asExpr<CallExpr>(expr.get());
    EXPECT_TRUE(call != nullptr);
    EXPECT_EQ(call->args.size(), 1u);

    auto *callee = asExpr<FieldExpr>(call->callee.get());
    EXPECT_TRUE(callee != nullptr);
    EXPECT_EQ(callee->field, "method");
}

TEST(PascalParserExpr, PointerDereference)
{
    auto expr = parseExpr("ptr^");
    EXPECT_TRUE(expr != nullptr);

    auto *deref = asExpr<DereferenceExpr>(expr.get());
    EXPECT_TRUE(deref != nullptr);

    auto *base = asExpr<NameExpr>(deref->operand.get());
    EXPECT_EQ(base->name, "ptr");
}

TEST(PascalParserExpr, AddressOf)
{
    auto expr = parseExpr("@x");
    EXPECT_TRUE(expr != nullptr);

    auto *addr = asExpr<AddressOfExpr>(expr.get());
    EXPECT_TRUE(addr != nullptr);

    auto *operand = asExpr<NameExpr>(addr->operand.get());
    EXPECT_EQ(operand->name, "x");
}

TEST(PascalParserExpr, SetConstructorEmpty)
{
    auto expr = parseExpr("[]");
    EXPECT_TRUE(expr != nullptr);

    auto *set = asExpr<SetConstructorExpr>(expr.get());
    EXPECT_TRUE(set != nullptr);
    EXPECT_TRUE(set->elements.empty());
}

TEST(PascalParserExpr, SetConstructorElements)
{
    auto expr = parseExpr("[1, 2, 3]");
    EXPECT_TRUE(expr != nullptr);

    auto *set = asExpr<SetConstructorExpr>(expr.get());
    EXPECT_TRUE(set != nullptr);
    EXPECT_EQ(set->elements.size(), 3u);
}

TEST(PascalParserExpr, SetConstructorRange)
{
    auto expr = parseExpr("[1..10]");
    EXPECT_TRUE(expr != nullptr);

    auto *set = asExpr<SetConstructorExpr>(expr.get());
    EXPECT_TRUE(set != nullptr);
    EXPECT_EQ(set->elements.size(), 1u);
    EXPECT_TRUE(set->elements[0].end != nullptr);
}

TEST(PascalParserExpr, ParenthesizedExpression)
{
    auto expr = parseExpr("(1 + 2) * 3");
    EXPECT_TRUE(expr != nullptr);

    auto *binary = asExpr<BinaryExpr>(expr.get());
    EXPECT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->op, BinaryExpr::Op::Mul);

    // Left should be (1 + 2)
    auto *left = asExpr<BinaryExpr>(binary->left.get());
    EXPECT_TRUE(left != nullptr);
    EXPECT_EQ(left->op, BinaryExpr::Op::Add);
}

//===----------------------------------------------------------------------===//
// Statement Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserStmt, AssignmentSimple)
{
    auto stmt = parseStmt("x := 1 + 2");
    EXPECT_TRUE(stmt != nullptr);

    auto *assign = asStmt<AssignStmt>(stmt.get());
    EXPECT_TRUE(assign != nullptr);

    auto *target = asExpr<NameExpr>(assign->target.get());
    EXPECT_EQ(target->name, "x");

    auto *value = asExpr<BinaryExpr>(assign->value.get());
    EXPECT_TRUE(value != nullptr);
    EXPECT_EQ(value->op, BinaryExpr::Op::Add);
}

TEST(PascalParserStmt, AssignmentField)
{
    auto stmt = parseStmt("obj.field := 42");
    EXPECT_TRUE(stmt != nullptr);

    auto *assign = asStmt<AssignStmt>(stmt.get());
    EXPECT_TRUE(assign != nullptr);

    auto *target = asExpr<FieldExpr>(assign->target.get());
    EXPECT_TRUE(target != nullptr);
    EXPECT_EQ(target->field, "field");
}

TEST(PascalParserStmt, AssignmentArrayElement)
{
    auto stmt = parseStmt("arr[i] := value");
    EXPECT_TRUE(stmt != nullptr);

    auto *assign = asStmt<AssignStmt>(stmt.get());
    EXPECT_TRUE(assign != nullptr);

    auto *target = asExpr<IndexExpr>(assign->target.get());
    EXPECT_TRUE(target != nullptr);
}

TEST(PascalParserStmt, ProcedureCallWithArgs)
{
    auto stmt = parseStmt("DoSomething(1, 2)");
    EXPECT_TRUE(stmt != nullptr);

    auto *call = asStmt<CallStmt>(stmt.get());
    EXPECT_TRUE(call != nullptr);

    auto *expr = asExpr<CallExpr>(call->call.get());
    EXPECT_TRUE(expr != nullptr);
    EXPECT_EQ(expr->args.size(), 2u);
}

TEST(PascalParserStmt, ProcedureCallNoParens)
{
    auto stmt = parseStmt("DoSomething");
    EXPECT_TRUE(stmt != nullptr);

    auto *call = asStmt<CallStmt>(stmt.get());
    EXPECT_TRUE(call != nullptr);

    auto *expr = asExpr<CallExpr>(call->call.get());
    EXPECT_TRUE(expr != nullptr);
    EXPECT_EQ(expr->args.size(), 0u);
}

TEST(PascalParserStmt, MethodCallStatement)
{
    auto stmt = parseStmt("obj.Method(x)");
    EXPECT_TRUE(stmt != nullptr);

    auto *call = asStmt<CallStmt>(stmt.get());
    EXPECT_TRUE(call != nullptr);

    auto *expr = asExpr<CallExpr>(call->call.get());
    EXPECT_TRUE(expr != nullptr);
}

TEST(PascalParserStmt, IfThen)
{
    auto stmt = parseStmt("if x > 0 then y := 1");
    EXPECT_TRUE(stmt != nullptr);

    auto *ifStmt = asStmt<IfStmt>(stmt.get());
    EXPECT_TRUE(ifStmt != nullptr);
    EXPECT_TRUE(ifStmt->condition != nullptr);
    EXPECT_TRUE(ifStmt->thenBranch != nullptr);
    EXPECT_TRUE(ifStmt->elseBranch == nullptr);
}

TEST(PascalParserStmt, IfThenElse)
{
    auto stmt = parseStmt("if x > 0 then y := 1 else y := 0");
    EXPECT_TRUE(stmt != nullptr);

    auto *ifStmt = asStmt<IfStmt>(stmt.get());
    EXPECT_TRUE(ifStmt != nullptr);
    EXPECT_TRUE(ifStmt->elseBranch != nullptr);
}

TEST(PascalParserStmt, NestedIfElse)
{
    // "if a then if b then c else d" - else binds to inner if
    auto stmt = parseStmt("if a then if b then x := 1 else x := 2");
    EXPECT_TRUE(stmt != nullptr);

    auto *outer = asStmt<IfStmt>(stmt.get());
    EXPECT_TRUE(outer != nullptr);
    EXPECT_TRUE(outer->elseBranch == nullptr);

    auto *inner = asStmt<IfStmt>(outer->thenBranch.get());
    EXPECT_TRUE(inner != nullptr);
    EXPECT_TRUE(inner->elseBranch != nullptr);
}

TEST(PascalParserStmt, WhileDo)
{
    auto stmt = parseStmt("while x > 0 do x := x - 1");
    EXPECT_TRUE(stmt != nullptr);

    auto *whileStmt = asStmt<WhileStmt>(stmt.get());
    EXPECT_TRUE(whileStmt != nullptr);
    EXPECT_TRUE(whileStmt->condition != nullptr);
    EXPECT_TRUE(whileStmt->body != nullptr);
}

TEST(PascalParserStmt, WhileDoBlock)
{
    auto stmt = parseStmt("while x > 0 do begin x := x - 1; y := y + 1 end");
    EXPECT_TRUE(stmt != nullptr);

    auto *whileStmt = asStmt<WhileStmt>(stmt.get());
    EXPECT_TRUE(whileStmt != nullptr);

    auto *body = asStmt<BlockStmt>(whileStmt->body.get());
    EXPECT_TRUE(body != nullptr);
    EXPECT_EQ(body->stmts.size(), 2u);
}

TEST(PascalParserStmt, RepeatUntil)
{
    auto stmt = parseStmt("repeat x := x + 1 until x > 10");
    EXPECT_TRUE(stmt != nullptr);

    auto *repeatStmt = asStmt<RepeatStmt>(stmt.get());
    EXPECT_TRUE(repeatStmt != nullptr);
    EXPECT_TRUE(repeatStmt->body != nullptr);
    EXPECT_TRUE(repeatStmt->condition != nullptr);
}

TEST(PascalParserStmt, RepeatUntilMultipleStatements)
{
    auto stmt = parseStmt("repeat x := x + 1; y := y - 1 until x > y");
    EXPECT_TRUE(stmt != nullptr);

    auto *repeatStmt = asStmt<RepeatStmt>(stmt.get());
    EXPECT_TRUE(repeatStmt != nullptr);

    auto *body = asStmt<BlockStmt>(repeatStmt->body.get());
    EXPECT_TRUE(body != nullptr);
    EXPECT_EQ(body->stmts.size(), 2u);
}

TEST(PascalParserStmt, ForTo)
{
    auto stmt = parseStmt("for i := 1 to 10 do sum := sum + i");
    EXPECT_TRUE(stmt != nullptr);

    auto *forStmt = asStmt<ForStmt>(stmt.get());
    EXPECT_TRUE(forStmt != nullptr);
    EXPECT_EQ(forStmt->loopVar, "i");
    EXPECT_EQ(forStmt->direction, ForDirection::To);
    EXPECT_TRUE(forStmt->start != nullptr);
    EXPECT_TRUE(forStmt->bound != nullptr);
    EXPECT_TRUE(forStmt->body != nullptr);
}

TEST(PascalParserStmt, ForDownto)
{
    auto stmt = parseStmt("for i := 10 downto 1 do sum := sum + i");
    EXPECT_TRUE(stmt != nullptr);

    auto *forStmt = asStmt<ForStmt>(stmt.get());
    EXPECT_TRUE(forStmt != nullptr);
    EXPECT_EQ(forStmt->direction, ForDirection::Downto);
}

TEST(PascalParserStmt, ForIn)
{
    auto stmt = parseStmt("for item in items do Process(item)");
    EXPECT_TRUE(stmt != nullptr);

    auto *forInStmt = asStmt<ForInStmt>(stmt.get());
    EXPECT_TRUE(forInStmt != nullptr);
    EXPECT_EQ(forInStmt->loopVar, "item");
    EXPECT_TRUE(forInStmt->collection != nullptr);
    EXPECT_TRUE(forInStmt->body != nullptr);
}

TEST(PascalParserStmt, BeginEnd)
{
    auto stmt = parseStmt("begin x := 1; y := 2; z := 3 end");
    EXPECT_TRUE(stmt != nullptr);

    auto *block = asStmt<BlockStmt>(stmt.get());
    EXPECT_TRUE(block != nullptr);
    EXPECT_EQ(block->stmts.size(), 3u);
}

TEST(PascalParserStmt, EmptyBlock)
{
    auto stmt = parseStmt("begin end");
    EXPECT_TRUE(stmt != nullptr);

    auto *block = asStmt<BlockStmt>(stmt.get());
    EXPECT_TRUE(block != nullptr);
    // May have one empty statement
}

TEST(PascalParserStmt, BreakStatement)
{
    auto stmt = parseStmt("break");
    EXPECT_TRUE(stmt != nullptr);

    auto *breakStmt = asStmt<BreakStmt>(stmt.get());
    EXPECT_TRUE(breakStmt != nullptr);
}

TEST(PascalParserStmt, ContinueStatement)
{
    auto stmt = parseStmt("continue");
    EXPECT_TRUE(stmt != nullptr);

    auto *continueStmt = asStmt<ContinueStmt>(stmt.get());
    EXPECT_TRUE(continueStmt != nullptr);
}

TEST(PascalParserStmt, RaiseStatement)
{
    auto stmt = parseStmt("raise Exception.Create('Error')");
    EXPECT_TRUE(stmt != nullptr);

    auto *raiseStmt = asStmt<RaiseStmt>(stmt.get());
    EXPECT_TRUE(raiseStmt != nullptr);
    EXPECT_TRUE(raiseStmt->exception != nullptr);
}

TEST(PascalParserStmt, RaiseReRaise)
{
    auto stmt = parseStmt("raise");
    EXPECT_TRUE(stmt != nullptr);

    auto *raiseStmt = asStmt<RaiseStmt>(stmt.get());
    EXPECT_TRUE(raiseStmt != nullptr);
    EXPECT_TRUE(raiseStmt->exception == nullptr);
}

//===----------------------------------------------------------------------===//
// Program Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserProg, MinimalProgram)
{
    auto prog = parseProg("program Hello; begin end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->name, "Hello");
    EXPECT_TRUE(prog->body != nullptr);
    EXPECT_TRUE(prog->usedUnits.empty());
}

TEST(PascalParserProg, ProgramWithUses)
{
    auto prog = parseProg("program Hello; uses Foo, Bar; begin end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->name, "Hello");
    EXPECT_EQ(prog->usedUnits.size(), 2u);
    EXPECT_EQ(prog->usedUnits[0], "Foo");
    EXPECT_EQ(prog->usedUnits[1], "Bar");
}

TEST(PascalParserProg, ProgramWithBody)
{
    auto prog = parseProg("program Hello; begin WriteLn('Hi') end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->name, "Hello");
    EXPECT_TRUE(prog->body != nullptr);
    EXPECT_EQ(prog->body->stmts.size(), 1u);

    auto *call = asStmt<CallStmt>(prog->body->stmts[0].get());
    EXPECT_TRUE(call != nullptr);
}

TEST(PascalParserProg, ProgramWithMultipleStatements)
{
    auto prog = parseProg("program Test;\n"
                          "begin\n"
                          "  x := 1;\n"
                          "  y := 2;\n"
                          "  WriteLn(x + y)\n"
                          "end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->name, "Test");
    EXPECT_EQ(prog->body->stmts.size(), 3u);
}

TEST(PascalParserProg, ProgramWithCompleteStructure)
{
    auto prog = parseProg("program Hello;\n"
                          "uses Foo, Bar;\n"
                          "begin\n"
                          "  WriteLn('Hi')\n"
                          "end.");
    EXPECT_TRUE(prog != nullptr);
    EXPECT_EQ(prog->name, "Hello");
    EXPECT_EQ(prog->usedUnits.size(), 2u);
    EXPECT_EQ(prog->usedUnits[0], "Foo");
    EXPECT_EQ(prog->usedUnits[1], "Bar");
    EXPECT_EQ(prog->body->stmts.size(), 1u);
}

//===----------------------------------------------------------------------===//
// Error Handling Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserError, MissingSemicolon)
{
    DiagnosticEngine diag;
    Lexer lexer("program Test begin end.", 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    // Should report error but may still parse
    EXPECT_TRUE(parser.hasError());
}

TEST(PascalParserError, UnexpectedToken)
{
    DiagnosticEngine diag;
    // Use an actual invalid expression start
    Lexer lexer("then 1", 0, diag);
    Parser parser(lexer, diag);
    auto expr = parser.parseExpression();
    EXPECT_TRUE(parser.hasError());
}

TEST(PascalParserError, MissingEnd)
{
    DiagnosticEngine diag;
    Lexer lexer("program Test; begin x := 1.", 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    EXPECT_TRUE(parser.hasError());
}

//===----------------------------------------------------------------------===//
// Case Statement Tests
//===----------------------------------------------------------------------===//

TEST(PascalParserCase, SimpleCaseWithIntegerLabels)
{
    auto stmt = parseStmt("case x of 1: y := 1; 2: y := 2 end");
    EXPECT_TRUE(stmt != nullptr);

    auto *cs = asStmt<CaseStmt>(stmt.get());
    EXPECT_TRUE(cs != nullptr);
    EXPECT_TRUE(cs->expr != nullptr);
    EXPECT_EQ(cs->arms.size(), 2u);
    EXPECT_TRUE(cs->elseBody == nullptr);

    // First arm: label is 1
    EXPECT_EQ(cs->arms[0].labels.size(), 1u);
    auto *label1 = asExpr<IntLiteralExpr>(cs->arms[0].labels[0].get());
    EXPECT_TRUE(label1 != nullptr);
    EXPECT_EQ(label1->value, 1);

    // Second arm: label is 2
    EXPECT_EQ(cs->arms[1].labels.size(), 1u);
    auto *label2 = asExpr<IntLiteralExpr>(cs->arms[1].labels[0].get());
    EXPECT_TRUE(label2 != nullptr);
    EXPECT_EQ(label2->value, 2);
}

TEST(PascalParserCase, CaseWithMultipleLabels)
{
    auto stmt = parseStmt("case x of 1, 2, 3: y := 10 end");
    EXPECT_TRUE(stmt != nullptr);

    auto *cs = asStmt<CaseStmt>(stmt.get());
    EXPECT_TRUE(cs != nullptr);
    EXPECT_EQ(cs->arms.size(), 1u);
    EXPECT_EQ(cs->arms[0].labels.size(), 3u);

    auto *l1 = asExpr<IntLiteralExpr>(cs->arms[0].labels[0].get());
    auto *l2 = asExpr<IntLiteralExpr>(cs->arms[0].labels[1].get());
    auto *l3 = asExpr<IntLiteralExpr>(cs->arms[0].labels[2].get());
    EXPECT_TRUE(l1 != nullptr && l1->value == 1);
    EXPECT_TRUE(l2 != nullptr && l2->value == 2);
    EXPECT_TRUE(l3 != nullptr && l3->value == 3);
}

TEST(PascalParserCase, CaseWithElse)
{
    auto stmt = parseStmt("case x of 1: y := 1 else y := 0 end");
    EXPECT_TRUE(stmt != nullptr);

    auto *cs = asStmt<CaseStmt>(stmt.get());
    EXPECT_TRUE(cs != nullptr);
    EXPECT_TRUE(cs->elseBody != nullptr);
    EXPECT_EQ(cs->arms.size(), 1u);
}

TEST(PascalParserCase, CaseWithEnumConstants)
{
    // Parser doesn't do semantic analysis - just verifies it parses identifiers as labels
    auto stmt = parseStmt("case c of Red: x := 1; Green: x := 2; Blue: x := 3 end");
    EXPECT_TRUE(stmt != nullptr);

    auto *cs = asStmt<CaseStmt>(stmt.get());
    EXPECT_TRUE(cs != nullptr);
    EXPECT_EQ(cs->arms.size(), 3u);

    // Check first label is a name expression
    auto *nameLabel = asExpr<NameExpr>(cs->arms[0].labels[0].get());
    EXPECT_TRUE(nameLabel != nullptr);
    EXPECT_EQ(nameLabel->name, "Red");
}

TEST(PascalParserCase, CaseWithBlockBody)
{
    auto stmt = parseStmt("case x of 1: begin y := 1; z := 2 end end");
    EXPECT_TRUE(stmt != nullptr);

    auto *cs = asStmt<CaseStmt>(stmt.get());
    EXPECT_TRUE(cs != nullptr);
    EXPECT_EQ(cs->arms.size(), 1u);

    // Body should be a BlockStmt
    auto *block = asStmt<BlockStmt>(cs->arms[0].body.get());
    EXPECT_TRUE(block != nullptr);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
