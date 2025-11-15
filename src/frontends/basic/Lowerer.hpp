//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Lowerer class, which transforms validated BASIC AST
// nodes into Viper Intermediate Language (IL) instructions.
//
// The lowerer is the final stage of the BASIC frontend compilation pipeline:
//   Lexer → Parser → AST → Semantic → Lowerer → IL
//
// Key Responsibilities:
// - AST-to-IL translation: Converts high-level BASIC constructs into IL
//   instructions that can be executed by the Viper VM or compiled to native code
// - Expression lowering: Transforms BASIC expressions (arithmetic, logical,
//   string operations) into IL register operations and value computations
// - Statement lowering: Converts BASIC statements (assignments, control flow,
//   I/O) into IL basic blocks with appropriate control flow edges
// - Runtime integration: Emits calls to the Viper runtime library for:
//   * String operations (concatenation, comparison, substring)
//   * I/O operations (PRINT, INPUT)
//   * Array operations (dynamic allocation, bounds checking)
//   * Built-in functions (mathematical, string manipulation, type conversion)
// - Memory management: Generates IL for variable allocation, array storage,
//   and string lifetime management
// - Control flow: Constructs IL basic blocks for:
//   * Conditional branches (IF/THEN/ELSE)
//   * Loops (FOR/NEXT, DO/LOOP, WHILE/WEND)
//   * Select statements (SELECT CASE)
//   * Procedure calls and returns
//
// IL Generation Strategy:
// - Procedures: Each BASIC SUB/FUNCTION becomes an IL function with proper
//   parameter passing and return value handling
// - Variables: BASIC variables map to IL locals (stack allocation) or globals
// - Arrays: Multi-dimensional arrays are lowered to runtime library calls
//   for allocation and element access with bounds checking
// - Labels and GOTO: BASIC labels become IL basic block labels with
//   appropriate branch instructions
// - Type system: BASIC type suffixes map to IL primitive types (i32, i64,
//   f32, f64, string)
//
// Runtime Library Integration:
// The lowerer generates calls to runtime functions for operations not directly
// expressible in IL:
// - String operations: viper_string_concat, viper_string_compare, etc.
// - I/O: viper_print, viper_input, viper_read, etc.
// - Arrays: viper_array_alloc, viper_array_get, viper_array_set
// - Built-ins: viper_math_sin, viper_string_left, etc.
//
// Design Notes:
// - The lowerer does not own the AST or IL module; it receives them as
//   parameters and populates the module with IL functions
// - Uses IRBuilder for efficient IL instruction emission
// - Maintains deterministic block label generation for reproducible output
// - Coordinates with NameMangler for consistent symbol naming across
//   compilation units
// - Supports optional bounds checking for array operations (debug mode)
//
// Lowering Pipeline Components:
// - LowerExprNumeric: Arithmetic and numeric operations
// - LowerExprLogical: Boolean and comparison operations
// - LowerExprBuiltin: Built-in function calls
// - LowerStmt_Core: Assignment and basic statements
// - LowerStmt_Control: Control flow (IF, FOR, WHILE, SELECT)
// - LowerStmt_IO: I/O operations (PRINT, INPUT)
// - LowerRuntime: Runtime library function declarations
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/BasicTypes.hpp"
#include "frontends/basic/EmitCommon.hpp"
#include "frontends/basic/IdentifierUtil.hpp"
#include "frontends/basic/LowerRuntime.hpp"
#include "frontends/basic/NameMangler.hpp"
#include "frontends/basic/Semantic_OOP.hpp"
#include "frontends/basic/TypeRules.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "viper/il/IRBuilder.hpp"
#include "viper/il/Module.hpp"
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
class SemanticAnalyzer;

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

    /// @brief Attach semantic analyzer for type information lookup during lowering.
    /// @param analyzer Semantic analyzer providing variable type information; may be nullptr.
    void setSemanticAnalyzer(const SemanticAnalyzer *analyzer) noexcept;

    /// @brief Access the semantic analyzer when present.
    [[nodiscard]] const SemanticAnalyzer *semanticAnalyzer() const noexcept;

    /// @brief Map a canonical (case-insensitive) qualified class name to its declared casing.
    /// @details Uses the OOP index to find the class by case-insensitive match and returns
    ///          the qualified name as declared. Falls back to the input when not found.
    std::string resolveQualifiedClassCasing(const std::string &qname) const;

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

    // Namespace qualification support -------------------------------------------------
    /// @brief Push a namespace path onto the current qualification stack.
    /// @param path Ordered segments, e.g. {"A","B"} for A.B.
    void pushNamespace(const std::vector<std::string> &path);

    /// @brief Pop @p count segments from the namespace stack (best-effort clamp).
    /// @param count Number of trailing segments to remove.
    void popNamespace(std::size_t count);

    /// @brief Qualify a class name with the active namespace path if any.
    /// @param klass Unqualified BASIC class name.
    /// @return Fully-qualified name like "A.B.Klass" or the original when no namespace active.
    [[nodiscard]] std::string qualify(const std::string &klass) const;

  private:
    friend class LocationScope;
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
        bool isStatic{false};           ///< True when symbol is a STATIC procedure-local variable.
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

    struct VariableStorage
    {
        SlotType slotInfo;
        Value pointer;
        bool isField{false};
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

    /// @brief Narrow a 64-bit value to 32 bits for runtime calls.
    /// @details Convenience wrapper around emitCommon().to_iN(value, 32) that reduces
    ///          boilerplate when preparing arguments for 32-bit runtime functions.
    /// @param value The 64-bit value to narrow.
    /// @param loc Source location for the narrowing operation.
    /// @return Narrowed 32-bit value.
    Value narrow32(Value value, il::support::SourceLoc loc);

    /// @brief Emit checked integer negation for @p val producing type @p ty.
    Value emitCheckedNeg(Type ty, Value val);

    void emitBr(BasicBlock *target);

    void emitCBr(Value cond, BasicBlock *t, BasicBlock *f);

    Value emitCallRet(Type ty, const std::string &callee, const std::vector<Value> &args);

    void emitCall(const std::string &callee, const std::vector<Value> &args);

    /// @brief Request runtime helper and emit call in a single operation.
    /// @details Combines requestHelper() and emitCallRet() to reduce boilerplate in
    ///          lowering code that needs to call runtime functions.
    /// @param feature Runtime feature to request (ensures helper is linked).
    /// @param callee Name of the runtime function to call.
    /// @param returnType Return type of the runtime function.
    /// @param args Arguments to pass to the runtime function.
    /// @return Value representing the call result.
    Value emitRuntimeHelper(il::runtime::RuntimeFeature feature,
                            const std::string &callee,
                            Type returnType,
                            const std::vector<Value> &args);

    /// @brief Emit an indirect call where the callee is a value operand.
    Value emitCallIndirectRet(Type ty, Value callee, const std::vector<Value> &args);

    /// @brief Emit a void-typed indirect call where the callee is a value operand.
    void emitCallIndirect(Value callee, const std::vector<Value> &args);

    Value emitConstStr(const std::string &globalName);
    void storeArray(Value slot, Value value, AstType elementType = AstType::I64);
    void storeArray(Value slot, Value value, AstType elementType, bool isObjectArray);
    void releaseArrayLocals(const std::unordered_set<std::string> &paramNames);
    void releaseArrayParams(const std::unordered_set<std::string> &paramNames);
    void releaseObjectLocals(const std::unordered_set<std::string> &paramNames);
    void releaseObjectParams(const std::unordered_set<std::string> &paramNames);
    void releaseDeferredTemps();
    void clearDeferredTemps();
    // Defer releases for temporaries
    void deferReleaseStr(Value v);
    void deferReleaseObj(Value v, const std::string &className = {});

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
    std::unordered_map<std::string, std::string> procNameAliases;

    std::unordered_map<const Stmt *, int> stmtVirtualLines_;
    int synthLineBase_{-1000000000};
    int synthSeq_{0};

    ProcedureContext context_;

    std::unique_ptr<lower::Emitter> emitter_;

    DiagnosticEmitter *diagnosticEmitter_{nullptr};
    const SemanticAnalyzer *semanticAnalyzer_{nullptr};

    // Names of module-level variables referenced inside procedures; used to
    // route main's accesses to runtime-backed storage for cross-proc sharing.
    std::unordered_set<std::string> crossProcGlobals_;

  public:
    void markCrossProcGlobal(const std::string &name)
    {
        crossProcGlobals_.insert(name);
    }

    bool isCrossProcGlobal(const std::string &name) const
    {
        return crossProcGlobals_.find(name) != crossProcGlobals_.end();
    }

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
        ArrayStrAlloc,
        ArrayStrRelease,
        ArrayStrGet,
        ArrayStrPut,
        ArrayStrLen,
        // Object arrays (ptr elements)
        ArrayObjNew,
        ArrayObjLen,
        ArrayObjGet,
        ArrayObjPut,
        ArrayObjResize,
        ArrayObjRelease,
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
        SleepMs,
        TimerMs,
        // Module-level variable address helpers
        ModvarAddrI64,
        ModvarAddrF64,
        ModvarAddrI1,
        ModvarAddrPtr,
        ModvarAddrStr,
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

    void requireArrayStrAlloc();
    void requireArrayStrRelease();
    void requireArrayStrGet();
    void requireArrayStrPut();
    void requireArrayStrLen();
    // Object array helpers
    void requireArrayObjNew();
    void requireArrayObjLen();
    void requireArrayObjGet();
    void requireArrayObjPut();
    void requireArrayObjResize();
    void requireArrayObjRelease();
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
    // Module-level globals helpers
    void requireModvarAddrI64();
    void requireModvarAddrF64();
    void requireModvarAddrI1();
    void requireModvarAddrPtr();
    void requireModvarAddrStr();
    // --- end: require declarations ---
    void requireStrRetainMaybe();
    void requireStrReleaseMaybe();
    void requireSleepMs();
    void requireTimerMs();
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
            /// @brief True when this field is declared as an array.
            /// @details Preserves array metadata from the AST so lowering can
            ///          distinguish implicit field-array accesses inside
            ///          methods (e.g., `inventory(i)`) from scalar fields.
            bool isArray{false};
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

    /// @brief Describes the address and type of a resolved member field.
    struct MemberFieldAccess
    {
        Value ptr;                          ///< Pointer to the field storage.
        Type ilType{Type(Type::Kind::I64)}; ///< IL type used for loads/stores.
        ::il::frontends::basic::Type astType{
            ::il::frontends::basic::Type::I64}; ///< Original AST type.
    };

    struct FieldScope
    {
        const ClassLayout *layout{nullptr};
        std::unordered_map<std::string, SymbolInfo> symbols;
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

    /// @brief Indexed CLASS metadata collected during scanning.
    OopIndex oopIndex_;

    /// @brief Stack of active field scopes while lowering class members.
    std::vector<FieldScope> fieldScopeStack_;

    /// @brief Active namespace path segments used for qualification during lowering.
    std::vector<std::string> nsStack_;

    /// @brief Stack of fully-qualified class names for access checking during lowering.
    std::vector<std::string> classStack_;

    /// @brief Determine the BASIC class associated with an object expression.
    [[nodiscard]] std::string resolveObjectClass(const Expr &expr) const;

    /// @brief Resolve pointer and type information for a member access expression.
    [[nodiscard]] std::optional<MemberFieldAccess> resolveMemberField(const MemberAccessExpr &expr);

    [[nodiscard]] std::optional<MemberFieldAccess> resolveImplicitField(std::string_view name,
                                                                        il::support::SourceLoc loc);
    [[nodiscard]] std::optional<VariableStorage> resolveVariableStorage(std::string_view name,
                                                                        il::support::SourceLoc loc);
    [[nodiscard]] const FieldScope *activeFieldScope() const;

  public:
    SymbolInfo &ensureSymbol(std::string_view name);

    SymbolInfo *findSymbol(std::string_view name);

    const SymbolInfo *findSymbol(std::string_view name) const;

    [[nodiscard]] bool isFieldInScope(std::string_view name) const;

    void pushFieldScope(const std::string &className);
    void popFieldScope();

    void markSymbolReferenced(std::string_view name);

    void markArray(std::string_view name);

    void markStatic(std::string_view name);

    void setSymbolType(std::string_view name, AstType type);

  private:
    SlotType getSlotType(std::string_view name) const;

    void resetSymbolState();

  public:
    /// @brief Lookup a cached procedure signature by BASIC name.
    /// @return Pointer to the signature when present, nullptr otherwise.
    const ProcedureSignature *findProcSignature(const std::string &name) const;

    /// @brief Resolve the emitted callee name to a qualified form when known.
    [[nodiscard]] std::string resolveCalleeName(const std::string &name) const;

    /// @brief Lookup the AST return type recorded for a class method.
    std::optional<::il::frontends::basic::Type> findMethodReturnType(
        std::string_view className, std::string_view methodName) const;

    /// @brief Lookup the AST type for a class field.
    std::optional<::il::frontends::basic::Type> findFieldType(std::string_view className,
                                                              std::string_view fieldName) const;

    /// @brief Check if a class field is an array.
    [[nodiscard]] bool isFieldArray(std::string_view className, std::string_view fieldName) const;

    [[nodiscard]] ProcedureContext &context() noexcept;

    [[nodiscard]] const ProcedureContext &context() const noexcept;

    // Class access control context -----------------------------------------------------
    /// @brief Push the fully-qualified class name currently being lowered.
    void pushClass(const std::string &qname)
    {
        classStack_.push_back(qname);
    }

    /// @brief Pop the current class lowering context.
    void popClass()
    {
        if (!classStack_.empty())
            classStack_.pop_back();
    }

    /// @brief Return the active class lowering context, or empty when not in a class.
    [[nodiscard]] std::string currentClass() const
    {
        return classStack_.empty() ? std::string() : classStack_.back();
    }
};

} // namespace il::frontends::basic

#include "frontends/basic/LoweringPipeline.hpp"
