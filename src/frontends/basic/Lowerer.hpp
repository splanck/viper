// File: src/frontends/basic/Lowerer.hpp
// Purpose: Declares lowering from BASIC AST to IL with helper routines and
// centralized runtime declarations.
// Key invariants: Procedure block labels are deterministic.
// Ownership/Lifetime: Lowerer does not own AST or module.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/NameMangler.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::frontends::basic
{

/// @brief Lowers BASIC AST into IL Module.
/// @invariant Generates deterministic block names per procedure using BlockNamer.
/// @ownership Owns produced Module; uses IRBuilder for structure emission.
class Lowerer
{
  public:
    /// @brief Construct a lowerer.
    /// @param boundsChecks Enable debug array bounds checks.
    explicit Lowerer(bool boundsChecks = false);

    /// @brief Lower @p prog into an IL module with @main entry.
    /// @notes Procedures are lowered before a synthetic `@main` encompassing
    ///        the program's top-level statements.
    il::core::Module lowerProgram(const Program &prog);

    /// @brief Backward-compatibility wrapper for older call sites.
    il::core::Module lower(const Program &prog);

  private:
    using Module = il::core::Module;
    using Function = il::core::Function;
    using BasicBlock = il::core::BasicBlock;
    using Value = il::core::Value;
    using Type = il::core::Type;
    using Opcode = il::core::Opcode;

    struct RVal
    {
        Value value;
        Type type;
    };

    /// @brief Layout of blocks emitted for an IF/ELSEIF chain.
    struct IfBlocks
    {
        std::vector<size_t> tests; ///< indexes of test blocks
        std::vector<size_t> thens; ///< indexes of THEN blocks
        BasicBlock *elseBlk;       ///< pointer to ELSE block
        BasicBlock *exitBlk;       ///< pointer to common exit
    };

    /// @brief Deterministic per-procedure block name generator.
    /// @invariant `k` starts at 0 per procedure and increases monotonically.
    ///            WHILE, FOR, and synthetic call continuations share the same
    ///            sequence to reflect lexical ordering.
    /// @ownership Owned by Lowerer; scoped to a single procedure.
    struct BlockNamer
    {
        std::string proc;        ///< procedure name
        unsigned ifCounter{0};   ///< sequential IF identifiers
        unsigned loopCounter{0}; ///< WHILE/FOR/call_cont identifiers
        std::unordered_map<std::string, unsigned> genericCounters; ///< other shapes

        explicit BlockNamer(std::string p) : proc(std::move(p)) {}

        std::string entry() const
        {
            return "entry_" + proc;
        }

        std::string ret() const
        {
            return "ret_" + proc;
        }

        std::string line(int line) const
        {
            return "L" + std::to_string(line) + "_" + proc;
        }

        unsigned nextIf()
        {
            return ifCounter++;
        }

        std::string ifTest(unsigned id) const
        {
            return "if_test_" + std::to_string(id) + "_" + proc;
        }

        std::string ifThen(unsigned id) const
        {
            return "if_then_" + std::to_string(id) + "_" + proc;
        }

        std::string ifElse(unsigned id) const
        {
            return "if_else_" + std::to_string(id) + "_" + proc;
        }

        std::string ifEnd(unsigned id) const
        {
            return "if_end_" + std::to_string(id) + "_" + proc;
        }

        unsigned nextWhile()
        {
            return loopCounter++;
        }

        std::string whileHead(unsigned id) const
        {
            return "while_head_" + std::to_string(id) + "_" + proc;
        }

        std::string whileBody(unsigned id) const
        {
            return "while_body_" + std::to_string(id) + "_" + proc;
        }

        std::string whileEnd(unsigned id) const
        {
            return "while_end_" + std::to_string(id) + "_" + proc;
        }

        unsigned nextFor()
        {
            return loopCounter++;
        }

        /// @brief Allocate next sequential ID for a call continuation.
        unsigned nextCall()
        {
            return loopCounter++;
        }

        std::string forHead(unsigned id) const
        {
            return "for_head_" + std::to_string(id) + "_" + proc;
        }

        std::string forBody(unsigned id) const
        {
            return "for_body_" + std::to_string(id) + "_" + proc;
        }

        std::string forInc(unsigned id) const
        {
            return "for_inc_" + std::to_string(id) + "_" + proc;
        }

        std::string forEnd(unsigned id) const
        {
            return "for_end_" + std::to_string(id) + "_" + proc;
        }

        /// @brief Build label for a synthetic call continuation block.
        std::string callCont(unsigned id) const
        {
            return "call_cont_" + std::to_string(id) + "_" + proc;
        }

        std::string generic(const std::string &hint)
        {
            auto &n = genericCounters[hint];
            std::string label = hint + "_" + std::to_string(n++) + "_" + proc;
            return label;
        }

        std::string tag(const std::string &base) const
        {
            return base + "_" + proc;
        }
    };

    struct ForBlocks
    {
        size_t headIdx{0};
        size_t headPosIdx{0};
        size_t headNegIdx{0};
        size_t bodyIdx{0};
        size_t incIdx{0};
        size_t doneIdx{0};
    };

    std::unique_ptr<BlockNamer> blockNamer;

    void collectVars(const Program &prog);
    void collectVars(const std::vector<const Stmt *> &stmts);
    void lowerFunctionDecl(const FunctionDecl &decl);
    void lowerSubDecl(const SubDecl &decl);
    /// @brief Stack-allocate parameters and seed local map.
    void materializeParams(const std::vector<Param> &params);
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

    // Built-in helpers
    RVal lowerLen(const BuiltinCallExpr &expr);
    RVal lowerMid(const BuiltinCallExpr &expr);
    RVal lowerLeft(const BuiltinCallExpr &expr);
    RVal lowerRight(const BuiltinCallExpr &expr);
    RVal lowerStr(const BuiltinCallExpr &expr);
    RVal lowerVal(const BuiltinCallExpr &expr);
    RVal lowerInt(const BuiltinCallExpr &expr);
    RVal lowerSqr(const BuiltinCallExpr &expr);
    RVal lowerAbs(const BuiltinCallExpr &expr);
    RVal lowerFloor(const BuiltinCallExpr &expr);
    RVal lowerCeil(const BuiltinCallExpr &expr);
    RVal lowerSin(const BuiltinCallExpr &expr);
    RVal lowerCos(const BuiltinCallExpr &expr);
    RVal lowerPow(const BuiltinCallExpr &expr);
    RVal lowerRnd(const BuiltinCallExpr &expr);

    // Shared argument helpers
    RVal lowerArg(const BuiltinCallExpr &c, size_t idx);
    RVal ensureI64(RVal v, il::support::SourceLoc loc);
    RVal ensureF64(RVal v, il::support::SourceLoc loc);

    void lowerLet(const LetStmt &stmt);
    void lowerPrint(const PrintStmt &stmt);
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
    void lowerFor(const ForStmt &stmt);
    void lowerForConstStep(const ForStmt &stmt, Value slot, RVal end, RVal step, long stepConst);
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

    build::IRBuilder *builder{nullptr};
    Module *mod{nullptr};
    Function *func{nullptr};
    BasicBlock *cur{nullptr};
    size_t fnExit{0};
    NameMangler mangler;
    std::unordered_map<int, size_t> lineBlocks;
    std::unordered_map<std::string, unsigned> varSlots;
    std::unordered_map<std::string, unsigned> arrayLenSlots;
    std::unordered_map<std::string, std::string> strings;
    std::unordered_set<std::string> vars;
    std::unordered_set<std::string> arrays;
    il::support::SourceLoc curLoc{}; ///< current source location for emitted IR
    bool boundsChecks{false};
    unsigned boundsCheckId{0};

    // runtime requirement tracking
    bool needInputLine{false};
    bool needRtToInt{false};
    bool needRtIntToStr{false};
    bool needRtF64ToStr{false};
    bool needAlloc{false};
    bool needRtStrEq{false};
    bool needRtConcat{false};

    enum class RuntimeFn
    {
        Sqrt,
        AbsI64,
        AbsF64,
        Floor,
        Ceil,
        Sin,
        Cos,
        Pow,
        RandomizeI64,
        Rnd,
    };

    struct RuntimeFnHash
    {
        size_t operator()(RuntimeFn f) const
        {
            return static_cast<size_t>(f);
        }
    };

    std::vector<RuntimeFn> runtimeOrder;
    std::unordered_set<RuntimeFn, RuntimeFnHash> runtimeSet;

    void trackRuntime(RuntimeFn fn);

    enum class ExprType
    {
        I64,
        F64,
        Str,
        Bool,
    };
    ExprType scanExpr(const Expr &e);
    ExprType scanUnaryExpr(const UnaryExpr &u);
    ExprType scanBinaryExpr(const BinaryExpr &b);
    ExprType scanArrayExpr(const ArrayExpr &arr);
    ExprType scanBuiltinCallExpr(const BuiltinCallExpr &c);
    void scanStmt(const Stmt &s);
    /// @brief Analyze @p prog for runtime usage prior to emission.
    void scanProgram(const Program &prog);
    /// @brief Emit IR for @p prog after scanning.
    void emitProgram(const Program &prog);
    void declareRequiredRuntime(build::IRBuilder &b);
};

} // namespace il::frontends::basic
