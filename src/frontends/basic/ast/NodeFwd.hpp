// File: src/frontends/basic/ast/NodeFwd.hpp
// Purpose: Forward declarations for BASIC front-end AST nodes and visitors.
// Key invariants: Provides minimal type visibility without forcing full AST definitions.
// Ownership/Lifetime: Callers own nodes through std::unique_ptr handles declared here.
// Links: docs/codemap.md
#pragma once

#include <memory>
#include <string>

namespace il::frontends::basic
{

struct Expr;
struct ExprVisitor;
struct MutExprVisitor;
struct IntExpr;
struct FloatExpr;
struct StringExpr;
struct BoolExpr;
struct VarExpr;
struct ArrayExpr;
struct UnaryExpr;
struct BinaryExpr;
struct BuiltinCallExpr;
struct LBoundExpr;
struct UBoundExpr;
struct CallExpr;
struct NewExpr;
struct MeExpr;
struct MemberAccessExpr;
struct MethodCallExpr;

struct Stmt;
struct StmtVisitor;
struct MutStmtVisitor;
struct LabelStmt;
struct PrintStmt;
struct PrintChStmt;
struct CallStmt;
struct ClsStmt;
struct ColorStmt;
struct LocateStmt;
struct LetStmt;
struct DimStmt;
struct ReDimStmt;
struct RandomizeStmt;
struct IfStmt;
struct WhileStmt;
struct DoStmt;
struct ForStmt;
struct NextStmt;
struct ExitStmt;
struct GotoStmt;
struct GosubStmt;
struct EndStmt;
struct OpenStmt;
struct CloseStmt;
struct SeekStmt;
struct OnErrorGoto;
struct Resume;
struct InputStmt;
struct InputChStmt;
struct LineInputChStmt;
struct ReturnStmt;
struct SelectCaseStmt;
struct StmtList;
struct DeleteStmt;
struct ConstructorDecl;
struct DestructorDecl;
struct MethodDecl;
struct ClassDecl;
struct TypeDecl;
struct PrintItem;
struct CaseArm;
struct NameRef;
struct Param;
struct FunctionDecl;
struct SubDecl;
struct Program;

enum class Type : int
{
    I64,
    F64,
    Str,
    Bool,
};

using ExprPtr = std::unique_ptr<Expr>;
using LValuePtr = ExprPtr;
using StmtPtr = std::unique_ptr<Stmt>;
using ProcDecl = StmtPtr;
using Identifier = std::string;

} // namespace il::frontends::basic

