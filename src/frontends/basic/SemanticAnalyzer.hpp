//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the SemanticAnalyzer class, which performs semantic
// analysis and validation of BASIC AST nodes.
//
// The semantic analyzer is the third stage of the BASIC compilation pipeline:
//   Lexer → Parser → AST → Semantic → Lowerer → IL
//
// Key Responsibilities:
// - Symbol resolution: Validates that all referenced variables, procedures,
//   and labels are properly declared before use
// - Type checking: Ensures type compatibility in expressions, assignments,
//   and procedure calls according to BASIC's type system
// - Scope management: Tracks local/global scopes, procedure parameters,
//   and nested block structures
// - Name resolution: Resolves identifiers to their declarations, handling
//   BASIC's implicit type suffix rules (%, &, !, #, $)
// - Array validation: Checks array dimension counts and bounds specifications
// - Control flow validation: Ensures proper nesting of FOR/NEXT, DO/LOOP,
//   WHILE/WEND, SELECT CASE constructs
// - Procedure validation: Verifies RETURN statements, parameter counts,
//   and function return value requirements
//
// Two-Pass Analysis:
// The analyzer operates in two passes to handle forward references:
// 1. Declaration pass: Collects all SUB/FUNCTION declarations and global
//    variables to build the symbol table
// 2. Validation pass: Checks statement bodies, resolves references, and
//    validates type consistency
//
// Type System Integration:
// - Implicit typing: Variables without explicit DIM use type suffixes
//   (%, &, !, #, $) or default to single-precision floating point
// - Explicit typing: DIM statements declare variable types explicitly
// - Type promotion: Numeric operations may promote integer types to
//   floating-point following BASIC's coercion rules
// - String operations: Validated separately from numeric operations
//
// Error Reporting:
// - Emits structured diagnostics via DiagnosticEmitter for:
//   * Undefined variables and procedures
//   * Type mismatches in expressions and assignments
//   * Invalid control flow (NEXT without FOR, etc.)
//   * Duplicate declarations
//   * Invalid array usage (wrong dimension count, bounds errors)
//
// Design Notes:
// - The analyzer does not modify the AST; it only validates and annotates
//   the symbol table for use by the lowering stage
// - Borrows DiagnosticEmitter for error reporting; does not own it
// - Uses ProcRegistry for tracking procedure signatures
// - Uses ScopeTracker for managing lexical scopes during analysis
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/basic/BasicTypes.hpp"
#include "frontends/basic/OopIndex.hpp"
#include "frontends/basic/ProcRegistry.hpp"
#include "frontends/basic/ScopeTracker.hpp"
#include "frontends/basic/SemanticDiagnostics.hpp"
#include "frontends/basic/ast/NodeFwd.hpp"
#include "frontends/basic/sem/NamespaceRegistry.hpp"
#include "frontends/basic/sem/TypeResolver.hpp"
#include "frontends/basic/sem/UsingContext.hpp"
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace il::frontends::basic
{
class SemanticAnalyzer;

namespace sem
{
class ControlCheckContext;
class ExprCheckContext;
} // namespace sem

/// @brief Metadata for multi-dimensional arrays.
/// @details Stores extent values and computed total size for array allocation.
struct ArrayMetadata
{
    /// Extent values for each dimension (empty for dynamic/unknown size).
    std::vector<long long> extents;

    /// Total number of elements (product of extents), or -1 if dynamic/unknown.
    long long totalSize{-1};

    /// Construct with unknown size (for dynamic arrays or runtime-computed extents).
    ArrayMetadata() = default;

    /// Construct with single dimension (backward compatible).
    explicit ArrayMetadata(long long size) : extents{size}, totalSize{size} {}

    /// Construct with multiple dimensions and pre-computed total.
    ArrayMetadata(std::vector<long long> exts, long long total)
        : extents(std::move(exts)), totalSize(total)
    {
    }
};

class SemanticAnalyzerExprVisitor;
class SemanticAnalyzerStmtVisitor;

namespace semantic_analyzer_detail
{
struct ExprRule;
class StmtShared;
const ExprRule &exprRule(BinaryExpr::Op op);
} // namespace semantic_analyzer_detail

/// @brief Traverses BASIC AST to collect symbols and labels, validate variable
///        references, and verify FOR/NEXT nesting.
/// @invariant Symbol table only contains definitions; unknown uses report
///            diagnostics.
/// @ownership Borrows DiagnosticEmitter; AST not owned.
class SemanticAnalyzer
{
  public:
    /// @brief Diagnostic code for non-boolean conditional expressions.
    static constexpr std::string_view DiagNonBooleanCondition = "E1001";

    /// @brief Diagnostic code for non-boolean logical operands.
    static constexpr std::string_view DiagNonBooleanLogicalOperand = "E1002";

    /// @brief Diagnostic code for non-boolean NOT operand.
    static constexpr std::string_view DiagNonBooleanNotOperand = "E1003";

    /// @brief Diagnostic code for CASE labels that exceed the 32-bit signed range.
    static constexpr std::string_view DiagSelectCaseLabelRange = "B2012";

    enum class Type;

    struct SelectCaseSelectorInfo;
    struct SelectCaseArmContext;

    /// @brief Create analyzer reporting to @p emitter.
    explicit SemanticAnalyzer(DiagnosticEmitter &emitter);

    /// @brief Analyze @p prog collecting symbols and labels.
    /// @param prog Program AST to walk.
    void analyze(const Program &prog);

    /// @brief Collected variable names defined in the program.
    const std::unordered_set<std::string> &symbols() const;

    /// @brief Line numbers present in the program.
    const std::unordered_set<int> &labels() const;

    /// @brief GOTO targets referenced in the program.
    const std::unordered_set<int> &labelRefs() const;

    /// @brief Registered procedures and their signatures.
    const ProcTable &procs() const;

    /// @brief Get canonical lowercase USING import namespaces from file scope.
    /// @return Set of imported namespace paths (e.g., "viper.terminal").
    std::vector<std::string> getUsingImports() const;

    /// @brief Lookup array metadata for a given array name.
    /// @param name Array identifier to look up.
    /// @return Pointer to ArrayMetadata if found, nullptr otherwise.
    const ArrayMetadata *lookupArrayMetadata(const std::string &name) const;

  private:
    friend class sem::ControlCheckContext;
    friend class sem::ExprCheckContext;
    friend class SemanticAnalyzerExprVisitor;
    friend class SemanticAnalyzerStmtVisitor;
    friend const semantic_analyzer_detail::ExprRule &semantic_analyzer_detail::exprRule(
        BinaryExpr::Op op);
    friend class semantic_analyzer_detail::StmtShared;

    /// @brief Record symbols and labels from a statement.
    /// @param s Statement node to analyze.
    void visitStmt(Stmt &s);

    /// @brief Perform semantic checks specific to CLS statements.
    void visit(const ClsStmt &s);
    /// @brief Perform semantic checks specific to COLOR statements.
    void visit(const ColorStmt &s);
    /// @brief Perform semantic checks specific to SLEEP statements.
    void visit(const SleepStmt &s);
    /// @brief Perform semantic checks specific to LOCATE statements.
    void visit(const LocateStmt &s);
    /// @brief Perform semantic checks specific to CURSOR statements.
    void visit(const CursorStmt &s);
    /// @brief Perform semantic checks specific to TRY/CATCH statements.
    void visit(const TryCatchStmt &s);
    /// @brief Perform semantic checks specific to USING statements.
    void visit(const UsingStmt &s);

    /// @brief Analyze statement list @p s.
    void analyzeStmtList(const StmtList &s);
    /// @brief Analyze PRINT statement @p s.
    void analyzePrint(const PrintStmt &s);
    /// @brief Analyze PRINT # statement @p s.
    void analyzePrintCh(const PrintChStmt &s);
    /// @brief Analyze CALL statement @p s.
    void analyzeCallStmt(CallStmt &s);
    /// @brief Analyze LET statement @p s.
    void analyzeLet(LetStmt &s);
    /// @brief Analyze OPEN statement @p s.
    void analyzeOpen(OpenStmt &s);
    /// @brief Analyze CLOSE statement @p s.
    void analyzeClose(CloseStmt &s);
    /// @brief Analyze SEEK statement @p s.
    void analyzeSeek(SeekStmt &s);
    /// @brief Analyze IF statement @p s.
    void analyzeIf(const IfStmt &s);
    /// @brief Analyze SELECT CASE statement @p s.
    void analyzeSelectCase(const SelectCaseStmt &s);
    /// @brief Analyze the statements contained within a CASE arm or ELSE body.
    void analyzeSelectCaseBody(const std::vector<StmtPtr> &body);
    SelectCaseSelectorInfo classifySelectCaseSelector(const SelectCaseStmt &s);
    bool validateSelectCaseArm(const CaseArm &arm, SelectCaseArmContext &ctx);
    bool validateSelectCaseStringArm(const CaseArm &arm, SelectCaseArmContext &ctx);
    bool validateSelectCaseNumericArm(const CaseArm &arm, SelectCaseArmContext &ctx);
    /// @brief Analyze WHILE statement @p s.
    void analyzeWhile(const WhileStmt &s);
    /// @brief Analyze DO statement @p s.
    void analyzeDo(const DoStmt &s);
    /// @brief Analyze FOR statement @p s.
    void analyzeFor(ForStmt &s);
    /// @brief Analyze FOR EACH statement @p s.
    void analyzeForEach(ForEachStmt &s);
    /// @brief Analyze GOTO statement @p s.
    void analyzeGoto(const GotoStmt &s);
    /// @brief Analyze GOSUB statement @p s.
    void analyzeGosub(const GosubStmt &s);
    /// @brief Analyze ON ERROR GOTO statement @p s.
    void analyzeOnErrorGoto(const OnErrorGoto &s);
    /// @brief Analyze NEXT statement @p s.
    void analyzeNext(const NextStmt &s);
    /// @brief Analyze EXIT statement @p s.
    void analyzeExit(const ExitStmt &s);
    /// @brief Analyze END statement @p s.
    void analyzeEnd(const EndStmt &s);
    /// @brief Analyze RESUME statement @p s.
    void analyzeResume(const Resume &s);
    /// @brief Analyze RETURN statement @p s.
    void analyzeReturn(ReturnStmt &s);
    /// @brief Analyze RANDOMIZE statement @p s.
    void analyzeRandomize(const RandomizeStmt &s);
    /// @brief Analyze INPUT statement @p s.
    void analyzeInput(InputStmt &s);
    /// @brief Analyze INPUT # statement @p s.
    void analyzeInputCh(InputChStmt &s);
    /// @brief Analyze LINE INPUT # statement @p s.
    void analyzeLineInputCh(LineInputChStmt &s);
    /// @brief Analyze DIM statement @p s.
    void analyzeDim(DimStmt &s);
    /// @brief Analyze CONST statement @p s.
    void analyzeConst(ConstStmt &s);
    /// @brief Analyze STATIC statement @p s.
    void analyzeStatic(StaticStmt &s);
    void analyzeShared(SharedStmt &s);
    /// @brief Analyze REDIM statement @p s.
    void analyzeReDim(ReDimStmt &s);
    /// @brief Analyze SWAP statement @p s.
    void analyzeSwap(SwapStmt &s);

    /// @brief Analyze namespace declaration (sets sawDecl_).
    void analyzeNamespaceDecl(NamespaceDecl &decl);
    /// @brief Analyze class declaration (sets sawDecl_).
    void analyzeClassDecl(ClassDecl &decl);
    /// @brief Analyze interface declaration (sets sawDecl_).
    void analyzeInterfaceDecl(InterfaceDecl &decl);
    /// @brief Analyze USING directive with placement and reserved-root validation.
    void analyzeUsingDecl(UsingDecl &decl);

    /// @brief Resolve a type reference and emit diagnostics if not found.
    /// @param typeName Type name to resolve (simple or qualified).
    /// @param currentNsChain Current namespace context (segments).
    /// @param loc Source location for diagnostics.
    /// @param length Source length for diagnostics.
    /// @return Fully-qualified type name if resolved; empty string on error.
    std::string resolveTypeRef(const std::string &typeName,
                               const std::vector<std::string> &currentNsChain,
                               il::support::SourceLoc loc,
                               uint32_t length);

    /// @brief Analyze assignment to a simple variable in LET.
    void analyzeVarAssignment(VarExpr &v, const LetStmt &s);
    /// @brief Analyze assignment to an array element in LET.
    void analyzeArrayAssignment(ArrayExpr &a, const LetStmt &s);
    /// @brief Analyze assignment to an object field in LET.
    void analyzeMemberAssignment(MemberAccessExpr &m, const LetStmt &s);
    /// @brief Report error for LET with a non-assignable left-hand side.
    void analyzeConstExpr(const LetStmt &s);

  public:
    /// @brief Inferred BASIC value type.
    enum class Type
    {
        Int,
        Float,
        String,
        Bool,
        ArrayInt,
        ArrayString,
        Object,
        Unknown
    };

    /// @brief Look up the tracked type for @p name when available.
    /// @param name Symbol whose type should be queried.
    /// @return Inferred type when recorded; std::nullopt otherwise.
    std::optional<Type> lookupVarType(const std::string &name) const;

    /// @brief Check if a symbol is registered at module level.
    ///
    /// @details Used by the lowerer to determine whether a variable referenced
    ///          in a procedure should get a local slot or resolve to module storage.
    ///
    /// @param name Symbol identifier to query.
    /// @return True if the symbol exists in the module-level symbol table.
    [[nodiscard]] bool isModuleLevelSymbol(const std::string &name) const;

    /// @brief Check if a symbol is a module-level CONST.
    ///
    /// @details Returns true if the symbol was declared with CONST at module level.
    ///          This is used by the lowerer to distinguish between CONST references
    ///          (which should always read from rt_modvar) and local variables
    ///          (which may shadow CONSTs but use local storage for writes).
    ///
    /// @param name Symbol identifier to check.
    /// @return True when the symbol is a module-level CONST.
    [[nodiscard]] bool isConstSymbol(const std::string &name) const;

  private:
    class ProcedureScope
    {
      public:
        explicit ProcedureScope(SemanticAnalyzer &analyzer) noexcept;

        ProcedureScope(const ProcedureScope &) = delete;
        ProcedureScope &operator=(const ProcedureScope &) = delete;

        ~ProcedureScope() noexcept;

        void noteSymbolInserted(const std::string &name);
        void noteVarTypeMutation(const std::string &name, std::optional<Type> previous);
        void noteArrayMutation(const std::string &name, std::optional<ArrayMetadata> previous);
        void noteChannelMutation(long long channel, bool previouslyOpen);
        void noteLabelInserted(int label);
        void noteLabelRefInserted(int label);

      private:
        struct VarTypeDelta
        {
            std::string name;
            std::optional<Type> previous;
        };

        struct ArrayDelta
        {
            std::string name;
            std::optional<ArrayMetadata> previous;
        };

        struct ChannelDelta
        {
            long long channel;
            bool previouslyOpen;
        };

        SemanticAnalyzer &analyzer_;
        ProcedureScope *previous_{nullptr};
        size_t forStackDepth_{0};
        size_t loopStackDepth_{0};
        std::vector<std::string> newSymbols_;
        std::vector<int> newLabels_;
        std::vector<int> newLabelRefs_;
        std::vector<VarTypeDelta> varTypeDeltas_;
        std::vector<ArrayDelta> arrayDeltas_;
        std::vector<ChannelDelta> channelDeltas_;
        std::unordered_set<std::string> trackedVarTypes_;
        std::unordered_set<std::string> trackedArrays_;
        std::unordered_set<long long> trackedChannels_;
        bool previousHandlerActive_{false};
        std::optional<int> previousHandlerTarget_;
    };

    /// @brief Classify how a symbol should be tracked during resolution.
    enum class SymbolKind
    {
        Reference,  ///< Existing symbol use; do not declare or type.
        Definition, ///< Implicit definition; apply suffix defaults if unset.
        InputTarget ///< INPUT destination; force suffix-based defaults.
    };

    /// @brief Track active loop constructs for EXIT validation.
    enum class LoopKind
    {
        For,
        While,
        Do,
        Sub,
        Function
    };

    /// @brief Resolve @p name in current scope and update symbol/type tables.
    /// @param name Symbol to resolve; updated when a scoped alias exists.
    /// @param kind Handling strategy indicating whether defaults should apply.
    void resolveAndTrackSymbol(std::string &name, SymbolKind kind);

    /// @brief Validate variable references in @p e and recurse into subtrees.
    /// @param e Expression node to analyze.
    /// @return Inferred type of the expression.
    Type visitExpr(Expr &e);

    /// @brief Ensure a conditional expression yields a BOOLEAN result.
    /// @param expr Condition expression to analyze.
    void checkConditionExpr(Expr &expr);

    /// @brief Ensure expression @p expr evaluates to a numeric type.
    void requireNumeric(Expr &expr, std::string_view message);

    /// @brief Analyze variable reference.
    Type analyzeVar(VarExpr &v);
    /// @brief Analyze unary expression.
    Type analyzeUnary(const UnaryExpr &u);
    /// @brief Analyze binary expression.
    Type analyzeBinary(const BinaryExpr &b);
    /// @brief Analyze LBOUND expression.
    Type analyzeLBound(LBoundExpr &expr);
    /// @brief Analyze UBOUND expression.
    Type analyzeUBound(UBoundExpr &expr);
    /// @brief Analyze built-in function call.
    Type analyzeBuiltinCall(const BuiltinCallExpr &c);

  public:
    /// @brief Metadata describing a single builtin argument slot.
    struct BuiltinArgSpec
    {
        bool optional{false};         ///< Whether the argument may be omitted.
        const Type *allowed{nullptr}; ///< Pointer to allowed types array.
        std::size_t allowedCount{0};  ///< Number of entries in @ref allowed.
    };

    /// @brief Metadata describing builtin arity and result type.
    struct BuiltinSignature
    {
        std::size_t requiredArgs{0};              ///< Number of mandatory arguments.
        std::size_t optionalArgs{0};              ///< Number of optional arguments.
        const BuiltinArgSpec *arguments{nullptr}; ///< Per-position argument specs.
        std::size_t argumentCount{0};             ///< Total number of entries in @ref arguments.
        Type result{Type::Unknown};               ///< Result type reported when checks pass.
    };

    /// @brief Pointer-to-member hook used for builtin semantic handlers.
    using BuiltinAnalyzer = Type (SemanticAnalyzer::*)(const BuiltinCallExpr &,
                                                       const std::vector<Type> &,
                                                       const BuiltinSignature &);

    /// @brief Fetch semantic signature metadata for @p builtin.
    static const BuiltinSignature &builtinSignature(BuiltinCallExpr::Builtin builtin);

    /// @brief Analyze ABS builtin (custom result logic).
    Type analyzeAbs(const BuiltinCallExpr &c,
                    const std::vector<Type> &args,
                    const BuiltinSignature &signature);

    /// @brief Analyze INSTR builtin (front-loaded optional argument handling).
    Type analyzeInstr(const BuiltinCallExpr &c,
                      const std::vector<Type> &args,
                      const BuiltinSignature &signature);

  private:
    /// @brief Check argument count is within [@p min,@p max].
    bool checkArgCount(const BuiltinCallExpr &c,
                       const std::vector<Type> &args,
                       size_t min,
                       size_t max);
    /// @brief Verify argument @p idx is one of @p allowed types.
    bool checkArgType(const BuiltinCallExpr &c,
                      size_t idx,
                      Type argTy,
                      std::span<const Type> allowed);
    bool validateBuiltinArgs(const BuiltinCallExpr &c,
                             const std::vector<Type> &args,
                             const BuiltinSignature &signature);
    Type analyzeBuiltinWithSignature(const BuiltinCallExpr &c,
                                     const std::vector<Type> &args,
                                     const BuiltinSignature &signature);
    /// @brief Resolve callee of user-defined call.
    const ProcSignature *resolveCallee(const CallExpr &c, ProcSignature::Kind expectedKind);
    /// @brief Collect and validate argument types for user-defined call.
    std::vector<Type> checkCallArgs(const CallExpr &c, const ProcSignature *sig);
    /// @brief Infer return type for user-defined call.
    Type inferCallType(const CallExpr &c, const ProcSignature *sig);
    /// @brief Analyze user-defined procedure call.
    Type analyzeCall(const CallExpr &c);
    /// @brief Analyze constructor invocation expression.
    Type analyzeNew(NewExpr &expr);
    /// @brief Analyze array access expression.
    Type analyzeArray(ArrayExpr &a);

    /// @brief Record that @p expr should be implicitly converted to @p targetType.
    void markImplicitConversion(const Expr &expr, Type targetType);

    /// @brief Request that @p expr be wrapped in an implicit cast to @p target.
    ///
    /// @details Until the BASIC AST grows an explicit cast node this utility
    ///          records the intent as an implicit conversion so that lowering
    ///          can inject the appropriate runtime helper when emitting IL.
    ///
    /// @param expr Expression slated for conversion.
    /// @param target Semantic type to coerce the expression to.
    void insertImplicitCast(Expr &expr, Type target);

    /// @brief Determine if @p stmts guarantees a return value on all control paths.
    bool mustReturn(const std::vector<StmtPtr> &stmts) const;
    /// @brief Determine if single statement @p s guarantees a return value.
    bool mustReturn(const Stmt &s) const;

    /// @brief Install an active error handler targeting @p label.
    void installErrorHandler(int label);
    /// @brief Clear any active error handler.
    void clearErrorHandler();
    /// @brief Whether an error handler is currently active.
    bool hasActiveErrorHandler() const noexcept;

    /// @brief Push @p kind onto the loop stack for EXIT validation.
    void pushLoop(LoopKind kind);
    /// @brief Pop the most recent loop kind from the loop stack.
    void popLoop();
    /// @brief Track @p name as the active FOR loop variable.
    void pushForVariable(std::string_view name);
    /// @brief Remove the most recently tracked FOR loop variable.
    void popForVariable();
    /// @brief Determine if @p name is currently an active FOR loop variable.
    bool isLoopVariableActive(std::string_view name) const noexcept;
    /// @brief Check if a specific loop kind exists anywhere in the loop stack.
    bool hasLoopOfKind(LoopKind kind) const noexcept;

    /// @brief Shared setup/teardown for analyzing procedures.
    template <typename Proc, typename BodyCallback>
    void analyzeProcedureCommon(const Proc &proc,
                                BodyCallback &&bodyCheck,
                                std::optional<LoopKind> loopKind = std::nullopt);

    /// @brief Register parameter @p param in the current procedure scope.
    void registerProcedureParam(const Param &param);

    /// @brief Analyze body of FUNCTION @p f.
    void analyzeProc(const FunctionDecl &f);
    /// @brief Analyze body of SUB @p s.
    void analyzeProc(const SubDecl &s);

    SemanticDiagnostics de; ///< Diagnostic sink.
    ScopeTracker scopes_;
    ProcRegistry procReg_;
    OopIndex oopIndex_;
    NamespaceRegistry ns_; ///< Registry for declared namespaces and types.
    UsingContext usings_;  ///< File-scoped USING imports.

    struct UsingScope
    {
        std::unordered_set<std::string> imports; ///< Canonical lowercase qualified namespaces
        std::unordered_map<std::string, std::string> aliases; ///< alias (lower) -> qualified ns
        std::unordered_map<std::string, il::support::SourceLoc> aliasLoc; ///< alias -> loc
    };

    std::vector<UsingScope> usingStack_;     ///< Scoped USING contexts (inherit on namespace entry)
    std::vector<std::string> nsStack_;       ///< Current namespace path during analysis.
    std::unique_ptr<TypeResolver> resolver_; ///< Type resolver (constructed after declare pass).
    bool sawDecl_{false}; ///< Track if any declarations seen (for USING placement).
    std::unordered_set<std::string> symbols_;
    std::unordered_set<std::string> constants_; ///< Constant names declared with CONST.
    std::unordered_map<std::string, Type> varTypes_;
    std::unordered_map<std::string, ArrayMetadata>
        arrays_;                                 ///< Array metadata with extents and total size
    std::unordered_set<long long> openChannels_; ///< Channels opened by literal handles.
    std::unordered_set<int> labels_;
    std::unordered_set<int> labelRefs_;
    std::vector<std::string> forStack_; ///< Active FOR loop variables.
    std::vector<LoopKind> loopStack_;   ///< Active loop constructs for EXIT validation.
    std::unordered_map<const Expr *, Type> implicitConversions_;
    ProcedureScope *activeProcScope_{nullptr};
    bool errorHandlerActive_{false};
    std::optional<int> errorHandlerTarget_;
    const FunctionDecl *activeFunction_{nullptr};
    BasicType activeFunctionExplicitRet_{BasicType::Unknown};
    bool activeFunctionNameAssigned_{
        false}; ///< Track if function name was assigned (VB-style implicit return). (BUG-003)
};

} // namespace il::frontends::basic
