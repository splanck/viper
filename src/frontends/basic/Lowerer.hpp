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
#include "frontends/basic/TypeRules.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include <array>
#include <cstdint>
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
class ScanWalker;
struct ProgramLowering;
struct ProcedureLowering;
struct StatementLowering;
class DiagnosticEmitter;

namespace builtins
{
class LowerCtx;
} // namespace builtins

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

    /// @brief Attach diagnostic emitter used for frontend errors during lowering.
    /// @param emitter Diagnostic sink; may be nullptr to disable emission.
    void setDiagnosticEmitter(DiagnosticEmitter *emitter) noexcept;

    /// @brief Access the diagnostic emitter when present.
    [[nodiscard]] DiagnosticEmitter *diagnosticEmitter() const noexcept;

  private:
    friend class LowererExprVisitor;
    friend class LowererStmtVisitor;
    friend class ScanWalker;
    friend struct ProgramLowering;
    friend struct ProcedureLowering;
    friend struct StatementLowering;
    friend class builtins::LowerCtx;

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

    /// @brief Result of lowering an array access expression.
    struct ArrayAccess
    {
        Value base;  ///< Array handle loaded from the BASIC slot.
        Value index; ///< Zero-based element index, coerced to i64.
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

        unsigned nextDo();

        std::string doHead(unsigned id) const;

        std::string doBody(unsigned id) const;

        std::string doEnd(unsigned id) const;

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
        struct BlockNameState
        {
            void reset() noexcept;

            [[nodiscard]] std::unordered_map<int, size_t> &lineBlocks() noexcept;

            [[nodiscard]] const std::unordered_map<int, size_t> &lineBlocks() const noexcept;

            [[nodiscard]] BlockNamer *namer() noexcept;

            [[nodiscard]] const BlockNamer *namer() const noexcept;

            void setNamer(std::unique_ptr<BlockNamer> namer) noexcept;

            void resetNamer() noexcept;

          private:
            std::unordered_map<int, size_t> lineBlocks_;
            std::unique_ptr<BlockNamer> namer_;
        };

        struct LoopState
        {
            void reset() noexcept;

            void setFunction(Function *function) noexcept;

            void push(BasicBlock *exitBlock);

            void pop();

            [[nodiscard]] BasicBlock *current() const;

            void markTaken();

            void refresh(BasicBlock *exitBlock);

            [[nodiscard]] bool taken() const;

          private:
            Function *function_{nullptr};
            std::vector<size_t> exitTargetIdx_;
            std::vector<bool> exitTaken_;
        };

        struct ErrorHandlerState
        {
            void reset() noexcept;

            [[nodiscard]] bool active() const noexcept;

            void setActive(bool active) noexcept;

            [[nodiscard]] std::optional<size_t> activeIndex() const noexcept;

            void setActiveIndex(std::optional<size_t> index) noexcept;

            [[nodiscard]] std::optional<int> activeLine() const noexcept;

            void setActiveLine(std::optional<int> line) noexcept;

            [[nodiscard]] std::unordered_map<int, size_t> &blocks() noexcept;

            [[nodiscard]] const std::unordered_map<int, size_t> &blocks() const noexcept;

            [[nodiscard]] std::unordered_map<size_t, int> &handlerTargets() noexcept;

            [[nodiscard]] const std::unordered_map<size_t, int> &handlerTargets() const noexcept;

          private:
            bool active_{false};
            std::optional<size_t> activeIndex_{};
            std::optional<int> activeLine_{};
            std::unordered_map<int, size_t> blocks_;
            std::unordered_map<size_t, int> handlerTargets_;
        };

        /// @brief Reset to an empty state ready for a new procedure.
        void reset() noexcept;

        [[nodiscard]] Function *function() const noexcept;

        void setFunction(Function *function) noexcept;

        [[nodiscard]] BasicBlock *current() const noexcept;

        void setCurrent(BasicBlock *block) noexcept;

        [[nodiscard]] size_t exitIndex() const noexcept;

        void setExitIndex(size_t index) noexcept;

        [[nodiscard]] unsigned nextTemp() const noexcept;

        void setNextTemp(unsigned next) noexcept;

        [[nodiscard]] unsigned boundsCheckId() const noexcept;

        void setBoundsCheckId(unsigned id) noexcept;

        /// @brief Return the current bounds-check identifier and advance it.
        unsigned consumeBoundsCheckId() noexcept;

        [[nodiscard]] LoopState &loopState() noexcept;

        [[nodiscard]] const LoopState &loopState() const noexcept;

        [[nodiscard]] BlockNameState &blockNames() noexcept;

        [[nodiscard]] const BlockNameState &blockNames() const noexcept;

        [[nodiscard]] ErrorHandlerState &errorHandlers() noexcept;

        [[nodiscard]] const ErrorHandlerState &errorHandlers() const noexcept;

      private:
        Function *function_{nullptr};
        BasicBlock *current_{nullptr};
        size_t exitIndex_{0};
        unsigned nextTemp_{0};
        unsigned boundsCheckId_{0};
        BlockNameState blockNames_{};
        LoopState loopState_{};
        ErrorHandlerState errorHandlers_{};
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

    /// @brief Lower the power operator by invoking the runtime helper.
    /// @param expr Binary expression node.
    /// @param lhs Pre-lowered left-hand side.
    /// @param rhs Pre-lowered right-hand side.
    /// @return Resulting double-precision value.
    RVal lowerPowBinary(const BinaryExpr &expr, RVal lhs, RVal rhs);

    /// @brief Lower a built-in call expression.
    /// @param expr Built-in call expression node.
    /// @return Resulting value and type.
    RVal lowerBuiltinCall(const BuiltinCallExpr &expr);

    /// @brief Classify the numeric type of @p expr using BASIC promotion rules.
    /// @param expr Expression to inspect.
    /// @return Numeric type describing INTEGER, LONG, SINGLE, or DOUBLE semantics.
    TypeRules::NumericType classifyNumericType(const Expr &expr);

    /// @brief Lower a UBOUND query expression.
    /// @param expr UBOUND expression node naming the array.
    /// @return Resulting value and type.
    RVal lowerUBoundExpr(const UBoundExpr &expr);

    // Shared argument helpers
    RVal coerceToI64(RVal v, il::support::SourceLoc loc);

    RVal coerceToF64(RVal v, il::support::SourceLoc loc);

    RVal coerceToBool(RVal v, il::support::SourceLoc loc);

    RVal ensureI64(RVal v, il::support::SourceLoc loc);

    RVal ensureF64(RVal v, il::support::SourceLoc loc);

    RVal normalizeChannelToI32(RVal channel, il::support::SourceLoc loc);

    /// @brief Emit a runtime error branch skeleton with trap handling.
    /// @param err Error code value returned from the runtime helper.
    /// @param loc Source location used for diagnostics.
    /// @param labelStem Base string for fail/cont block labels.
    /// @param onFailure Callback executed after positioning at the fail block.
    void emitRuntimeErrCheck(Value err,
                             il::support::SourceLoc loc,
                             std::string_view labelStem,
                             const std::function<void(Value)> &onFailure);

    void lowerLet(const LetStmt &stmt);

    void lowerPrint(const PrintStmt &stmt);

    void lowerPrintCh(const PrintChStmt &stmt);

    void lowerStmtList(const StmtList &stmt);

    void lowerReturn(const ReturnStmt &stmt);

    void visit(const ClsStmt &stmt);

    void visit(const ColorStmt &stmt);

    void visit(const LocateStmt &stmt);

    void lowerOpen(const OpenStmt &stmt);

    void lowerClose(const CloseStmt &stmt);

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

    void lowerDo(const DoStmt &stmt);

    void lowerLoopBody(const std::vector<StmtPtr> &body);

    void lowerFor(const ForStmt &stmt);

    void lowerForConstStep(const ForStmt &stmt, Value slot, RVal end, RVal step, int64_t stepConst);

    void lowerForVarStep(const ForStmt &stmt, Value slot, RVal end, RVal step);

    ForBlocks setupForBlocks(bool varStep);

    void emitForStep(Value slot, Value step);

    void lowerNext(const NextStmt &stmt);

    void lowerExit(const ExitStmt &stmt);

    void lowerGoto(const GotoStmt &stmt);

    void lowerOnErrorGoto(const OnErrorGoto &stmt);

    void lowerResume(const Resume &stmt);

    void lowerEnd(const EndStmt &stmt);

    void lowerInput(const InputStmt &stmt);

    void lowerLineInputCh(const LineInputChStmt &stmt);

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

    Value emitConstI64(std::int64_t v);

    Value emitZext1ToI64(Value val);

    Value emitISub(Value lhs, Value rhs);

    Value emitBasicLogicalI64(Value b1);

    /// @brief Emit checked integer negation for @p val producing type @p ty.
    Value emitCheckedNeg(Type ty, Value val);

    void emitBr(BasicBlock *target);

    void emitCBr(Value cond, BasicBlock *t, BasicBlock *f);

    Value emitCallRet(Type ty, const std::string &callee, const std::vector<Value> &args);

    void emitCall(const std::string &callee, const std::vector<Value> &args);

    Value emitConstStr(const std::string &globalName);
    void storeArray(Value slot, Value value);
    void releaseArrayLocals(const std::unordered_set<std::string> &paramNames);
    void releaseArrayParams(const std::unordered_set<std::string> &paramNames);

    void emitTrap();

    void emitTrapFromErr(Value errCode);

    void emitEhPush(BasicBlock *handler);
    void emitEhPop();
    void emitEhPopForReturn();
    void clearActiveErrorHandler();
    BasicBlock *ensureErrorHandlerBlock(int targetLine);

    void emitRet(Value v);

    void emitRetVoid();

    std::string getStringLabel(const std::string &s);

    unsigned nextTempId();

    std::string nextFallbackBlockLabel();

    ArrayAccess lowerArrayAccess(const ArrayExpr &expr);

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
    size_t nextFallbackBlockId{0};
    std::unordered_map<std::string, ProcedureSignature> procSignatures;

    ProcedureContext context_;

    DiagnosticEmitter *diagnosticEmitter_{nullptr};

    // runtime requirement tracking
    using RuntimeFeature = il::runtime::RuntimeFeature;

    RuntimeHelperTracker runtimeTracker;

    enum class ManualRuntimeHelper : std::size_t
    {
        ArrayI32New = 0,
        ArrayI32Resize,
        ArrayI32Len,
        ArrayI32Get,
        ArrayI32Set,
        ArrayI32Retain,
        ArrayI32Release,
        ArrayOobPanic,
        OpenErrVstr,
        CloseErr,
        PrintlnChErr,
        LineInputChErr,
        Count
    };

    static constexpr std::size_t manualRuntimeHelperCount =
        static_cast<std::size_t>(ManualRuntimeHelper::Count);

    static constexpr std::size_t manualRuntimeHelperIndex(ManualRuntimeHelper helper) noexcept
    {
        return static_cast<std::size_t>(helper);
    }

    std::array<bool, manualRuntimeHelperCount> manualHelperRequirements_{};

    void setManualHelperRequired(ManualRuntimeHelper helper);
    [[nodiscard]] bool isManualHelperRequired(ManualRuntimeHelper helper) const;
    void resetManualHelpers();

    void requireArrayI32New();
    void requireArrayI32Resize();
    void requireArrayI32Len();
    void requireArrayI32Get();
    void requireArrayI32Set();
    void requireArrayI32Retain();
    void requireArrayI32Release();
    void requireArrayOobPanic();
    void requireOpenErrVstr();
    void requireCloseErr();
    void requirePrintlnChErr();
    void requireLineInputChErr();
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

    [[nodiscard]] ProcedureContext &context() noexcept;

    [[nodiscard]] const ProcedureContext &context() const noexcept;
};

} // namespace il::frontends::basic

#include "frontends/basic/LoweringPipeline.hpp"
