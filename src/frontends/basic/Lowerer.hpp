// File: src/frontends/basic/Lowerer.hpp
// Purpose: Declares lowering from BASIC AST to IL with helper routines and
// centralized runtime declarations.
// Key invariants: Procedure block labels are deterministic.
// Ownership/Lifetime: Lowerer does not own AST or module.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/LowerRuntime.hpp"
#include "frontends/basic/NameMangler.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::frontends::basic
{

class LowererExprVisitor;
class LowererStmtVisitor;
class ScanExprVisitor;
class ScanStmtVisitor;
struct ProgramLowering;
struct ProcedureLowering;
struct StatementLowering;

/// @brief Lowers BASIC AST into IL Module.
/// @invariant Generates deterministic block names per procedure using BlockNamer.
/// @ownership Owns produced Module; uses IRBuilder for structure emission.
class Lowerer
{
  public:
    /// @brief Construct a lowerer.
    /// @param boundsChecks Enable debug array bounds checks.
    explicit Lowerer(bool boundsChecks = false);

    ~Lowerer();

    /// @brief Lower @p prog into an IL module with @main entry.
    /// @notes Procedures are lowered before a synthetic `@main` encompassing
    ///        the program's top-level statements.
    il::core::Module lowerProgram(const Program &prog);

    /// @brief Backward-compatibility wrapper for older call sites.
    il::core::Module lower(const Program &prog);

  private:
    friend class LowererExprVisitor;
    friend class LowererStmtVisitor;
    friend class ScanExprVisitor;
    friend class ScanStmtVisitor;
    friend struct ProgramLowering;
    friend struct ProcedureLowering;
    friend struct StatementLowering;

    using Module = il::core::Module;
    using Function = il::core::Function;
    using BasicBlock = il::core::BasicBlock;
    using Value = il::core::Value;
    using Type = il::core::Type;
    using Opcode = il::core::Opcode;
    using IlValue = Value;
    using IlType = Type;
    using AstType = ::il::frontends::basic::Type;

  public:
    struct RVal
    {
        Value value;
        Type type;
    };

    /// @brief Aggregated metadata for a BASIC symbol.
    struct SymbolInfo
    {
        AstType type{AstType::I64};      ///< BASIC type derived from declarations or suffixes.
        bool hasType{false};             ///< True when @ref type was explicitly recorded.
        bool isArray{false};             ///< True when symbol refers to an array.
        bool isBoolean{false};           ///< True when scalar bool storage is required.
        bool referenced{false};          ///< Tracks whether lowering observed the symbol.
        std::optional<unsigned> slotId;  ///< Stack slot id for the variable when materialized.
        std::optional<unsigned> arrayLengthSlot; ///< Optional slot for array length (bounds checks).
        std::string stringLabel;         ///< Cached label for deduplicated string literals.
    };

  private:
    struct SlotType
    {
        Type type{Type(Type::Kind::I64)};
        bool isArray{false};
        bool isBoolean{false};
    };

    /// @brief Cached signature for a user-defined procedure.
    struct ProcedureSignature
    {
        Type retType{Type(Type::Kind::I64)};           ///< Declared return type.
        std::vector<Type> paramTypes;                  ///< Declared parameter types.
    };

  private:
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

        explicit BlockNamer(std::string p);

        std::string entry() const;

        std::string ret() const;

        std::string line(int line) const;

        unsigned nextIf();

        std::string ifTest(unsigned id) const;

        std::string ifThen(unsigned id) const;

        std::string ifElse(unsigned id) const;

        std::string ifEnd(unsigned id) const;

        unsigned nextWhile();

        std::string whileHead(unsigned id) const;

        std::string whileBody(unsigned id) const;

        std::string whileEnd(unsigned id) const;

        unsigned nextFor();

        /// @brief Allocate next sequential ID for a call continuation.
        unsigned nextCall();

        std::string forHead(unsigned id) const;

        std::string forBody(unsigned id) const;

        std::string forInc(unsigned id) const;

        std::string forEnd(unsigned id) const;

        /// @brief Build label for a synthetic call continuation block.
        std::string callCont(unsigned id) const;

        std::string generic(const std::string &hint);

        std::string tag(const std::string &base) const;
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

    /// @brief Aggregates mutable state for the procedure currently being lowered.
    /// @invariant Reset before each procedure to avoid leaking state across lowers.
    struct ProcedureContext
    {
        /// @brief Reset to an empty state ready for a new procedure.
        void reset() noexcept
        {
            function_ = nullptr;
            current_ = nullptr;
            exitIndex_ = 0;
            lineBlocks_.clear();
            blockNamer_.reset();
            nextTemp_ = 0;
            boundsCheckId_ = 0;
        }

        [[nodiscard]] Function *function() const noexcept { return function_; }

        void setFunction(Function *function) noexcept { function_ = function; }

        [[nodiscard]] BasicBlock *current() const noexcept { return current_; }

        void setCurrent(BasicBlock *block) noexcept { current_ = block; }

        [[nodiscard]] size_t exitIndex() const noexcept { return exitIndex_; }

        void setExitIndex(size_t index) noexcept { exitIndex_ = index; }

        [[nodiscard]] unsigned nextTemp() const noexcept { return nextTemp_; }

        void setNextTemp(unsigned next) noexcept { nextTemp_ = next; }

        [[nodiscard]] unsigned boundsCheckId() const noexcept { return boundsCheckId_; }

        void setBoundsCheckId(unsigned id) noexcept { boundsCheckId_ = id; }

        /// @brief Return the current bounds-check identifier and advance it.
        unsigned consumeBoundsCheckId() noexcept { return boundsCheckId_++; }

        [[nodiscard]] std::unordered_map<int, size_t> &lineBlocks() noexcept
        {
            return lineBlocks_;
        }

        [[nodiscard]] const std::unordered_map<int, size_t> &lineBlocks() const noexcept
        {
            return lineBlocks_;
        }

        [[nodiscard]] BlockNamer *blockNamer() noexcept { return blockNamer_.get(); }

        [[nodiscard]] const BlockNamer *blockNamer() const noexcept
        {
            return blockNamer_.get();
        }

        void setBlockNamer(std::unique_ptr<BlockNamer> namer) noexcept
        {
            blockNamer_ = std::move(namer);
        }

        void resetBlockNamer() noexcept { blockNamer_.reset(); }

      private:
        Function *function_{nullptr};
        BasicBlock *current_{nullptr};
        size_t exitIndex_{0};
        std::unordered_map<int, size_t> lineBlocks_;
        std::unique_ptr<BlockNamer> blockNamer_;
        unsigned nextTemp_{0};
        unsigned boundsCheckId_{0};
    };

  public:
    struct ProcedureConfig;

  private:
    struct ProcedureMetadata
    {
        std::vector<const Stmt *> bodyStmts;
        std::unordered_set<std::string> paramNames;
        std::vector<il::core::Param> irParams;
        size_t paramCount{0};
    };

    ProcedureMetadata collectProcedureMetadata(const std::vector<Param> &params,
                                               const std::vector<StmtPtr> &body,
                                               const ProcedureConfig &config);

    void buildProcedureSkeleton(Function &f,
                                const std::string &name,
                                const ProcedureMetadata &metadata);

    void allocateLocalSlots(const std::unordered_set<std::string> &paramNames,
                            bool includeParams);

    void lowerStatementSequence(const std::vector<const Stmt *> &stmts,
                                bool stopOnTerminated,
                                const std::function<void(const Stmt &)> &beforeBranch = {});

    void collectProcedureSignatures(const Program &prog);

    void collectVars(const Program &prog);

    void collectVars(const std::vector<const Stmt *> &stmts);

    void lowerFunctionDecl(const FunctionDecl &decl);

    void lowerSubDecl(const SubDecl &decl);

  public:
    /// @brief Configuration shared by FUNCTION and SUB lowering.
    struct ProcedureConfig
    {
        Type retType{Type(Type::Kind::Void)};          ///< IL return type for the procedure.
        std::function<void()> postCollect;             ///< Hook after variable discovery.
        std::function<void()> emitEmptyBody;           ///< Emit return path for empty bodies.
        std::function<void()> emitFinalReturn;         ///< Emit return in the synthetic exit block.
    };

  private:
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

    // Shared argument helpers
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

    void lowerReDim(const ReDimStmt &stmt);

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

    std::unique_ptr<ProgramLowering> programLowering;
    std::unique_ptr<ProcedureLowering> procedureLowering;
    std::unique_ptr<StatementLowering> statementLowering;

    build::IRBuilder *builder{nullptr};
    Module *mod{nullptr};
    NameMangler mangler;
    std::unordered_map<std::string, SymbolInfo> symbols;
    il::support::SourceLoc curLoc{}; ///< current source location for emitted IR
    bool boundsChecks{false};
    size_t nextStringId{0};
    std::unordered_map<std::string, ProcedureSignature> procSignatures;

    ProcedureContext context_;

    // runtime requirement tracking
    using RuntimeFeature = il::runtime::RuntimeFeature;

    RuntimeHelperTracker runtimeTracker;
    bool needsArrI32New{false};
    bool needsArrI32Resize{false};

    void requireArrayI32New();
    void requireArrayI32Resize();
    void requestHelper(RuntimeFeature feature);

    bool isHelperNeeded(RuntimeFeature feature) const;

    void trackRuntime(RuntimeFeature feature);

    void declareRequiredRuntime(build::IRBuilder &b);
#include "frontends/basic/LowerScan.hpp"

  public:
    SymbolInfo &ensureSymbol(std::string_view name);

    SymbolInfo *findSymbol(std::string_view name);

    const SymbolInfo *findSymbol(std::string_view name) const;

    void markSymbolReferenced(std::string_view name);

    void markArray(std::string_view name);

    void setSymbolType(std::string_view name, AstType type);

  private:
    SlotType getSlotType(std::string_view name) const;

    void resetSymbolState();

  public:
    /// @brief Lookup a cached procedure signature by BASIC name.
    /// @return Pointer to the signature when present, nullptr otherwise.
    const ProcedureSignature *findProcSignature(const std::string &name) const;

    [[nodiscard]] ProcedureContext &context() noexcept { return context_; }

    [[nodiscard]] const ProcedureContext &context() const noexcept { return context_; }
};

} // namespace il::frontends::basic

#include "frontends/basic/LoweringPipeline.hpp"
