// File: src/frontends/basic/Lowerer.hpp
// Purpose: Declares lowering from BASIC AST to IL with helper routines and
// centralized runtime declarations.
// Key invariants: Procedure block labels are deterministic.
// Ownership/Lifetime: Lowerer does not own AST or module.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/BasicTypes.hpp"
#include "frontends/basic/EmitCommon.hpp"
#include "frontends/basic/LowerRuntime.hpp"
#include "frontends/basic/NameMangler.hpp"
#include "frontends/basic/TypeRules.hpp"
#include "viper/il/IRBuilder.hpp"
#include "viper/il/Module.hpp"
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
struct ProgramLowering;
struct ProcedureLowering;
struct StatementLowering;
class DiagnosticEmitter;
class SelectCaseLowering;

struct LogicalExprLowering;
struct NumericExprLowering;
struct BuiltinExprLowering;

namespace builtins
{
class LowerCtx;
} // namespace builtins

namespace lower
{
class Emitter;
class BuiltinLowerContext;

namespace common
{
class CommonLowering;
} // namespace common

namespace detail
{
class ExprTypeScanner;
class RuntimeNeedsScanner;
} // namespace detail
} // namespace lower

class Emit;
enum class OverflowPolicy;
enum class Signedness;

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

    /// @brief Mark a symbol as storing an object reference.
    /// @param name BASIC symbol name tracked in the symbol table.
    /// @param className Fully-qualified BASIC class name associated with the object.
    void setSymbolObjectType(std::string_view name, std::string className);

    /// @brief Ensure the given runtime helper is available to lowering.
    /// @param feature Runtime feature required by an auxiliary pass.
    void requestRuntimeFeature(il::runtime::RuntimeFeature feature)
    {
        requestHelper(feature);
    }

  private:
    friend class LowererExprVisitor;
    friend class LowererStmtVisitor;
    friend class lower::detail::ExprTypeScanner;
    friend class lower::detail::RuntimeNeedsScanner;
    friend struct ProgramLowering;
    friend struct ProcedureLowering;
    friend struct StatementLowering;
    friend struct LogicalExprLowering;
    friend struct NumericExprLowering;
    friend struct BuiltinExprLowering;
    friend class builtins::LowerCtx;
    friend class SelectCaseLowering;
    friend class lower::Emitter;
    friend class lower::BuiltinLowerContext;
    friend class lower::common::CommonLowering;
    friend class Emit;

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

    /// @brief Classify how an array access will be consumed.
    enum class ArrayAccessKind
    {
        Load,  ///< The caller will read from the computed element.
        Store, ///< The caller will write to the computed element.
    };

    /// @brief Aggregated metadata for a BASIC symbol.
    struct SymbolInfo
    {
        AstType type{AstType::I64};     ///< BASIC type derived from declarations or suffixes.
        bool hasType{false};            ///< True when @ref type was explicitly recorded.
        bool isArray{false};            ///< True when symbol refers to an array.
        bool isBoolean{false};          ///< True when scalar bool storage is required.
        bool referenced{false};         ///< Tracks whether lowering observed the symbol.
        std::optional<unsigned> slotId; ///< Stack slot id for the variable when materialized.
        std::optional<unsigned>
            arrayLengthSlot;     ///< Optional slot for array length (bounds checks).
        std::string stringLabel; ///< Cached label for deduplicated string literals.
        bool isObject{false};    ///< True when symbol references an object slot.
        std::string objectClass; ///< Class name for object symbols; empty otherwise.
    };

  private:
    /// @brief Aggregates state shared across helper stages of program lowering.
    struct ProgramEmitContext
    {
        std::vector<const Stmt *> mainStmts;
        Function *function{nullptr};
        BasicBlock *entry{nullptr};
    };

    struct SlotType
    {
        Type type{Type(Type::Kind::I64)};
        bool isArray{false};
        bool isBoolean{false};
        bool isObject{false};
        std::string objectClass;
    };

    /// @brief Cached signature for a user-defined procedure.
    struct ProcedureSignature
    {
        Type retType{Type(Type::Kind::I64)}; ///< Declared return type.
        std::vector<Type> paramTypes;        ///< Declared parameter types.
    };

  private:
    /// @brief Layout of blocks emitted for an IF/ELSEIF chain.
    struct IfBlocks
    {
        std::vector<size_t> tests; ///< indexes of test blocks
        std::vector<size_t> thens; ///< indexes of THEN blocks
        size_t elseIdx;            ///< index of ELSE block
        size_t exitIdx;            ///< index of common exit block
    };

    /// @brief Deterministic per-procedure block naming and per-procedure context.
#include "frontends/basic/LowererContext.hpp"

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

    void allocateLocalSlots(const std::unordered_set<std::string> &paramNames, bool includeParams);

    void lowerStatementSequence(const std::vector<const Stmt *> &stmts,
                                bool stopOnTerminated,
                                const std::function<void(const Stmt &)> &beforeBranch = {});

    void collectProcedureSignatures(const Program &prog);

    void collectVars(const Program &prog);

    void collectVars(const std::vector<const Stmt *> &stmts);

    int virtualLine(const Stmt &s);

    void lowerFunctionDecl(const FunctionDecl &decl);

    void lowerSubDecl(const SubDecl &decl);

  public:
    /// @brief Configuration shared by FUNCTION and SUB lowering.
    struct ProcedureConfig
    {
        Type retType{Type(Type::Kind::Void)};  ///< IL return type for the procedure.
        std::function<void()> postCollect;     ///< Hook after variable discovery.
        std::function<void()> emitEmptyBody;   ///< Emit return path for empty bodies.
        std::function<void()> emitFinalReturn; ///< Emit return in the synthetic exit block.
    };

  private:
    /// @brief Lower shared procedure scaffolding for FUNCTION/SUB declarations.
    void lowerProcedure(const std::string &name,
                        const std::vector<Param> &params,
                        const std::vector<StmtPtr> &body,
                        const ProcedureConfig &config);

    /// @brief Collect declarations and cache main statement pointers.
    ProgramEmitContext collectProgramDeclarations(const Program &prog);

    /// @brief Create the main function skeleton and block mappings.
    void buildMainFunctionSkeleton(ProgramEmitContext &state);

    /// @brief Discover global variables referenced by the main body.
    void collectMainVariables(ProgramEmitContext &state);

    /// @brief Materialise stack storage for main locals and bookkeeping.
    void allocateMainLocals(ProgramEmitContext &state);

    /// @brief Emit the main body along with epilogue clean-up.
    void emitMainBodyAndEpilogue(ProgramEmitContext &state);

    /// @brief Stack-allocate parameters and seed local map.
    void materializeParams(const std::vector<Param> &params);

    /// @brief Reset procedure-level lowering state.
    void resetLoweringState();

    void lowerStmt(const Stmt &stmt);

    RVal lowerExpr(const Expr &expr);
    RVal lowerScalarExpr(const Expr &expr);
    RVal lowerScalarExpr(RVal value, il::support::SourceLoc loc);

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

    /// @brief Translate a BASIC return type hint into an IL type.
    /// @param fnName BASIC function name used for suffix inference.
    /// @param hint Explicit BASIC return type annotation, if any.
    /// @return IL type matching the BASIC semantics for the function return.
    Type functionRetTypeFromHint(const std::string &fnName, BasicType hint) const;

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

    /// @brief Lower a NEW expression allocating a BASIC object instance.
    RVal lowerNewExpr(const NewExpr &expr);

    /// @brief Lower a ME expression referencing the implicit instance slot.
    RVal lowerMeExpr(const MeExpr &expr);

    /// @brief Lower a member access reading a field from an object instance.
    RVal lowerMemberAccessExpr(const MemberAccessExpr &expr);

    /// @brief Lower an object method invocation expression.
    RVal lowerMethodCallExpr(const MethodCallExpr &expr);

    /// @brief Lower a DELETE statement releasing an object reference.
    void lowerDelete(const DeleteStmt &stmt);

    // Shared argument helpers
    RVal coerceToI64(RVal v, il::support::SourceLoc loc);

    RVal coerceToF64(RVal v, il::support::SourceLoc loc);

    RVal coerceToBool(RVal v, il::support::SourceLoc loc);

    RVal ensureI64(RVal v, il::support::SourceLoc loc);

    RVal ensureF64(RVal v, il::support::SourceLoc loc);

    struct PrintChArgString
    {
        Value text;
        std::optional<il::runtime::RuntimeFeature> feature;
    };

    PrintChArgString lowerPrintChArgToString(const Expr &expr, RVal value, bool quoteStrings);

    Value buildPrintChWriteRecord(const PrintChStmt &stmt);

#include "frontends/basic/LowerStmt_Core.hpp"

#include "frontends/basic/LowerStmt_Runtime.hpp"

#include "frontends/basic/LowerStmt_IO.hpp"

    /// @brief Emit blocks for an IF/ELSEIF chain.
    /// @param conds Number of conditions (IF + ELSEIFs).
    /// @return Indices for test/then blocks and ELSE/exit blocks.
#include "frontends/basic/LowerStmt_Control.hpp"

    void lowerSelectCase(const SelectCaseStmt &stmt);

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
    Emit emitCommon() noexcept;
    Emit emitCommon(il::support::SourceLoc loc) noexcept;

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
    void releaseObjectLocals(const std::unordered_set<std::string> &paramNames);
    void releaseObjectParams(const std::unordered_set<std::string> &paramNames);

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

    lower::Emitter &emitter() noexcept;
    const lower::Emitter &emitter() const noexcept;

    ArrayAccess lowerArrayAccess(const ArrayExpr &expr, ArrayAccessKind kind);

    void emitProgram(const Program &prog);

    void ensureGosubStack();

    std::unique_ptr<ProgramLowering> programLowering;
    std::unique_ptr<ProcedureLowering> procedureLowering;
    std::unique_ptr<StatementLowering> statementLowering;

    build::IRBuilder *builder{nullptr};
    Module *mod{nullptr};
    NameMangler mangler;
    std::unordered_map<std::string, SymbolInfo> symbols;
    il::support::SourceLoc curLoc{}; ///< current source location for emitted IR
    bool boundsChecks{false};
    static constexpr int kGosubStackDepth = 128;
    size_t nextStringId{0};
    size_t nextFallbackBlockId{0};
    std::unordered_map<std::string, ProcedureSignature> procSignatures;

    std::unordered_map<const Stmt *, int> stmtVirtualLines_;
    int synthLineBase_{-1000000000};
    int synthSeq_{0};

    ProcedureContext context_;

    std::unique_ptr<lower::Emitter> emitter_;

    DiagnosticEmitter *diagnosticEmitter_{nullptr};

    // runtime requirement tracking
    using RuntimeFeature = il::runtime::RuntimeFeature;

    RuntimeHelperTracker runtimeTracker;

    enum class ManualRuntimeHelper : std::size_t
    {
        Trap = 0,
        ArrayI32New,
        ArrayI32Resize,
        ArrayI32Len,
        ArrayI32Get,
        ArrayI32Set,
        ArrayI32Retain,
        ArrayI32Release,
        ArrayOobPanic,
        OpenErrVstr,
        CloseErr,
        SeekChErr,
        WriteChErr,
        PrintlnChErr,
        LineInputChErr,
        EofCh,
        LofCh,
        LocCh,
        StrRetainMaybe,
        StrReleaseMaybe,
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

    void requireTrap();
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
    void requireSeekChErr();
    void requireWriteChErr();
    void requirePrintlnChErr();
    void requireLineInputChErr();
    // --- begin: require declarations ---
    void requireEofCh();
    void requireLofCh();
    void requireLocCh();
    // --- end: require declarations ---
    void requireStrRetainMaybe();
    void requireStrReleaseMaybe();
    void requestHelper(RuntimeFeature feature);

    bool isHelperNeeded(RuntimeFeature feature) const;

    void trackRuntime(RuntimeFeature feature);

    void declareRequiredRuntime(build::IRBuilder &b);
#include "frontends/basic/LowerScan.hpp"
  public:
    /// @brief Computed memory layout for a BASIC CLASS or TYPE declaration.
    struct ClassLayout
    {
        /// @brief Metadata describing a single field within the class layout.
        struct Field
        {
            std::string name;
            AstType type{AstType::I64};
            std::size_t offset{0};
            std::size_t size{0};
        };

        /// @brief Ordered field entries preserving declaration order.
        std::vector<Field> fields;
        /// @brief Mapping from field name to its index within @ref fields.
        std::unordered_map<std::string, std::size_t> fieldIndex;
        /// @brief Total storage size in bytes rounded up to the alignment requirement.
        std::size_t size{0};
        /// @brief Stable identifier assigned during OOP scanning for runtime dispatch.
        std::int64_t classId{0};

        [[nodiscard]] const Field *findField(std::string_view name) const
        {
            auto it = fieldIndex.find(std::string(name));
            if (it == fieldIndex.end())
                return nullptr;
            return &fields[it->second];
        }
    };

  private:
    /// @brief Scan program OOP constructs to populate class layouts and runtime requests.
    void scanOOP(const Program &prog);

    /// @brief Emit constructor, destructor, and method bodies for CLASS declarations.
    void emitOopDeclsAndBodies(const Program &prog);

    void emitClassConstructor(const ClassDecl &klass, const ConstructorDecl &ctor);

    void emitClassDestructor(const ClassDecl &klass, const DestructorDecl *userDtor);

    void emitClassMethod(const ClassDecl &klass, const MethodDecl &method);

    unsigned materializeSelfSlot(const std::string &className, Function &fn);

    Value loadSelfPointer(unsigned slotId);

    void emitFieldReleaseSequence(Value selfPtr, const ClassLayout &layout);

    /// @brief Cached layout table indexed by class or TYPE name.
    std::unordered_map<std::string, ClassLayout> classLayouts_;

    /// @brief Determine the BASIC class associated with an object expression.
    [[nodiscard]] std::string resolveObjectClass(const Expr &expr) const;

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
