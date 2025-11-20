// File: src/frontends/basic/ast/NodeFwd.hpp
// Purpose: Provides forward declarations and common type aliases for BASIC AST nodes.
// Key invariants: Type enumerators align with BASIC scalar kinds.
// Ownership/Lifetime: Nodes are owned via std::unique_ptr by callers.
// Links: docs/codemap.md
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace il::frontends::basic
{

struct Expr;
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
struct IsExpr;
struct AsExpr;

struct ExprVisitor;
struct MutExprVisitor;

struct Stmt;
struct LabelStmt;
struct PrintStmt;
struct PrintChStmt;
struct BeepStmt;
struct CallStmt;
struct ClsStmt;
struct ColorStmt;
struct SleepStmt;
struct LocateStmt;
struct CursorStmt;
struct AltScreenStmt;
struct LetStmt;
struct ConstStmt;
struct DimStmt;
struct StaticStmt;
struct SharedStmt;
struct ReDimStmt;
struct SwapStmt;
struct RandomizeStmt;
struct IfStmt;
struct SelectCaseStmt;
struct WhileStmt;
struct DoStmt;
struct ForStmt;
struct NextStmt;
struct ExitStmt;
struct GotoStmt;
struct GosubStmt;
struct OpenStmt;
struct CloseStmt;
struct SeekStmt;
struct OnErrorGoto;
struct Resume;
struct EndStmt;
struct InputStmt;
struct InputChStmt;
struct LineInputChStmt;
struct ReturnStmt;
struct StmtList;
struct DeleteStmt;
struct ConstructorDecl;
struct DestructorDecl;
struct MethodDecl;
struct PropertyDecl;
struct ClassDecl;
struct TypeDecl;
struct InterfaceDecl;
struct PrintItem;
struct CaseArm;
struct NameRef;

struct StmtVisitor;
struct MutStmtVisitor;

struct Param;
struct FunctionDecl;
struct SubDecl;
struct Program;

/// @brief BASIC primitive types mirrored by the AST.
enum class Type : std::uint8_t
{
    I64,
    F64,
    Str,
    Bool,
};

using ExprPtr = std::unique_ptr<Expr>;
using LValuePtr = ExprPtr;
using Identifier = std::string;
using StmtPtr = std::unique_ptr<Stmt>;
using ProcDecl = StmtPtr;

void visit(const Expr &expr, ExprVisitor &visitor);
void visit(Expr &expr, MutExprVisitor &visitor);
void visit(const Stmt &stmt, StmtVisitor &visitor);
void visit(Stmt &stmt, MutStmtVisitor &visitor);

} // namespace il::frontends::basic
