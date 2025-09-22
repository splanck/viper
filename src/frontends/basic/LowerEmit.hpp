// File: src/frontends/basic/LowerEmit.hpp
// Purpose: Declares IR emission helpers and lowering routines for BASIC.
// Key invariants: Control-flow labels remain deterministic via BlockNamer.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/class-catalog.md
#pragma once

private:
void collectVars(const Program &prog);
void collectVars(const std::vector<const Stmt *> &stmts);
void lowerFunctionDecl(const FunctionDecl &decl);
void lowerSubDecl(const SubDecl &decl);
/// @brief Configuration shared by FUNCTION and SUB lowering.
struct ProcedureConfig
{
    Type retType{Type(Type::Kind::Void)}; ///< IL return type for the procedure.
    std::function<void()> postCollect;    ///< Hook after variable discovery.
    std::function<void()> emitEmptyBody;  ///< Emit return path for empty bodies.
    std::function<void()> emitFinalReturn;///< Emit return in the synthetic exit block.
};
/// @brief Lower shared procedure scaffolding for FUNCTION/SUB declarations.
void lowerProcedure(const std::string &name,
                    const std::vector<Param> &params,
                    const std::vector<StmtPtr> &body,
                    const ProcedureConfig &config);
/// @brief Stack-allocate parameters and seed local map.
void materializeParams(const std::vector<Param> &params);
/// @brief Reset procedure-level lowering state.
void resetLoweringState();
void lowerStmt(const Stmt &stmt);
RVal lowerExpr(const Expr &expr);
/// @brief Lower a variable reference expression.
/// @param expr Variable expression node.
/// @return Loaded value and its type.
RVal lowerVarExpr(const VarExpr &expr);
/// @brief Lower a unary expression (e.g. NOT).
/// @param expr Unary expression node.
/// @return Resulting value and type.
RVal lowerUnaryExpr(const UnaryExpr &expr);
/// @brief Lower a binary expression.
/// @param expr Binary expression node.
/// @return Resulting value and type.
RVal lowerBinaryExpr(const BinaryExpr &expr);
/// @brief Lower a boolean expression using explicit branch bodies.
/// @param cond Boolean condition selecting THEN branch.
/// @param loc Source location to attribute to control flow.
/// @param emitThen Lambda emitting THEN branch body, storing into result slot.
/// @param emitElse Lambda emitting ELSE branch body, storing into result slot.
/// @param thenLabelBase Optional label base for THEN block naming.
/// @param elseLabelBase Optional label base for ELSE block naming.
/// @param joinLabelBase Optional label base for join block naming.
/// @return Resulting value and type.
RVal lowerBoolBranchExpr(Value cond,
                         il::support::SourceLoc loc,
                         const std::function<void(Value)> &emitThen,
                         const std::function<void(Value)> &emitElse,
                         std::string_view thenLabelBase = {},
                         std::string_view elseLabelBase = {},
                         std::string_view joinLabelBase = {});
/// @brief Lower logical (`AND`/`OR`) expressions with short-circuiting.
/// @param expr Binary expression node.
/// @return Resulting value and type.
RVal lowerLogicalBinary(const BinaryExpr &expr);
/// @brief Lower integer division and modulo with divide-by-zero check.
/// @param expr Binary expression node.
/// @return Resulting value and type.
RVal lowerDivOrMod(const BinaryExpr &expr);
/// @brief Lower string concatenation and equality/inequality comparisons.
/// @param expr Binary expression node.
/// @param lhs Pre-lowered left-hand side.
/// @param rhs Pre-lowered right-hand side.
/// @return Resulting value and type.
RVal lowerStringBinary(const BinaryExpr &expr, RVal lhs, RVal rhs);
/// @brief Lower numeric arithmetic and comparisons.
/// @param expr Binary expression node.
/// @param lhs Pre-lowered left-hand side.
/// @param rhs Pre-lowered right-hand side.
/// @return Resulting value and type.
RVal lowerNumericBinary(const BinaryExpr &expr, RVal lhs, RVal rhs);
/// @brief Lower a built-in call expression.
/// @param expr Built-in call expression node.
/// @return Resulting value and type.
RVal lowerBuiltinCall(const BuiltinCallExpr &expr);

public:
// Built-in helpers
RVal lowerLen(const BuiltinCallExpr &expr);
RVal lowerMid(const BuiltinCallExpr &expr);
RVal lowerLeft(const BuiltinCallExpr &expr);
RVal lowerRight(const BuiltinCallExpr &expr);
RVal lowerStr(const BuiltinCallExpr &expr);
RVal lowerVal(const BuiltinCallExpr &expr);
RVal lowerInt(const BuiltinCallExpr &expr);
RVal lowerInstr(const BuiltinCallExpr &expr);
RVal lowerLtrim(const BuiltinCallExpr &expr);
RVal lowerRtrim(const BuiltinCallExpr &expr);
RVal lowerTrim(const BuiltinCallExpr &expr);
RVal lowerUcase(const BuiltinCallExpr &expr);
RVal lowerLcase(const BuiltinCallExpr &expr);
RVal lowerChr(const BuiltinCallExpr &expr);
RVal lowerAsc(const BuiltinCallExpr &expr);
RVal lowerSqr(const BuiltinCallExpr &expr);
RVal lowerAbs(const BuiltinCallExpr &expr);
RVal lowerFloor(const BuiltinCallExpr &expr);
RVal lowerCeil(const BuiltinCallExpr &expr);
RVal lowerSin(const BuiltinCallExpr &expr);
RVal lowerCos(const BuiltinCallExpr &expr);
RVal lowerPow(const BuiltinCallExpr &expr);
RVal lowerRnd(const BuiltinCallExpr &expr);

private:
// Shared argument helpers
RVal lowerArg(const BuiltinCallExpr &c, size_t idx);
RVal coerceToI64(RVal v, il::support::SourceLoc loc);
RVal coerceToF64(RVal v, il::support::SourceLoc loc);
RVal coerceToBool(RVal v, il::support::SourceLoc loc);
RVal ensureI64(RVal v, il::support::SourceLoc loc);
RVal ensureF64(RVal v, il::support::SourceLoc loc);

void lowerLet(const LetStmt &stmt);
void lowerPrint(const PrintStmt &stmt);
void lowerStmtList(const StmtList &stmt);
void lowerReturn(const ReturnStmt &stmt);
/// @brief Emit blocks for an IF/ELSEIF chain.
/// @param conds Number of conditions (IF + ELSEIFs).
/// @return Indices for test/then blocks and ELSE/exit blocks.
IfBlocks emitIfBlocks(size_t conds);
/// @brief Evaluate @p cond and branch to @p thenBlk or @p falseBlk.
void lowerIfCondition(const Expr &cond,
                      BasicBlock *testBlk,
                      BasicBlock *thenBlk,
                      BasicBlock *falseBlk,
                      il::support::SourceLoc loc);
/// @brief Lower a THEN/ELSE branch and link to exit.
/// @return True if branch falls through to @p exitBlk.
bool lowerIfBranch(const Stmt *stmt,
                   BasicBlock *thenBlk,
                   BasicBlock *exitBlk,
                   il::support::SourceLoc loc);
void lowerIf(const IfStmt &stmt);
void lowerWhile(const WhileStmt &stmt);
void lowerLoopBody(const std::vector<StmtPtr> &body);
void lowerFor(const ForStmt &stmt);
void lowerForConstStep(const ForStmt &stmt, Value slot, RVal end, RVal step, int64_t stepConst);
void lowerForVarStep(const ForStmt &stmt, Value slot, RVal end, RVal step);
ForBlocks setupForBlocks(bool varStep);
void emitForStep(Value slot, Value step);
void lowerNext(const NextStmt &stmt);
void lowerGoto(const GotoStmt &stmt);
void lowerEnd(const EndStmt &stmt);
void lowerInput(const InputStmt &stmt);
void lowerDim(const DimStmt &stmt);
void lowerRandomize(const RandomizeStmt &stmt);

// helpers
IlType ilBoolTy();
IlValue emitBoolConst(bool v);
IlValue emitBoolFromBranches(const std::function<void(Value)> &emitThen,
                             const std::function<void(Value)> &emitElse,
                             std::string_view thenLabelBase = "bool_then",
                             std::string_view elseLabelBase = "bool_else",
                             std::string_view joinLabelBase = "bool_join");
Value emitAlloca(int bytes);
Value emitLoad(Type ty, Value addr);
void emitStore(Type ty, Value addr, Value val);
Value emitBinary(Opcode op, Type ty, Value lhs, Value rhs);
/// @brief Emit unary instruction of @p op on @p val producing @p ty.
Value emitUnary(Opcode op, Type ty, Value val);
void emitBr(BasicBlock *target);
void emitCBr(Value cond, BasicBlock *t, BasicBlock *f);
Value emitCallRet(Type ty, const std::string &callee, const std::vector<Value> &args);
void emitCall(const std::string &callee, const std::vector<Value> &args);
Value emitConstStr(const std::string &globalName);
void emitTrap();
void emitRet(Value v);
void emitRetVoid();
std::string getStringLabel(const std::string &s);
unsigned nextTempId();
Value lowerArrayAddr(const ArrayExpr &expr);
void emitProgram(const Program &prog);
