// File: src/frontends/basic/SemanticAnalyzer.hpp
// Purpose: Declares BASIC semantic analyzer for symbol and label tracking,
//          basic validation, and two-pass procedure registration.
// Key invariants: Analyzer tracks defined symbols and reports unknown
//                 references.
// Ownership/Lifetime: Analyzer borrows DiagnosticEmitter; no AST ownership.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/ProcRegistry.hpp"
#include "frontends/basic/ScopeTracker.hpp"
#include "frontends/basic/SemanticDiagnostics.hpp"
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::frontends::basic
{

class SemanticAnalyzerExprVisitor;
class SemanticAnalyzerStmtVisitor;

namespace semantic_analyzer_detail
{
struct ExprRule;
const ExprRule &exprRule(BinaryExpr::Op op);
}

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

  private:
    friend class SemanticAnalyzerExprVisitor;
    friend class SemanticAnalyzerStmtVisitor;
    friend const semantic_analyzer_detail::ExprRule &
    semantic_analyzer_detail::exprRule(BinaryExpr::Op op);

    /// @brief Record symbols and labels from a statement.
    /// @param s Statement node to analyze.
    void visitStmt(Stmt &s);

    /// @brief Analyze statement list @p s.
    void analyzeStmtList(const StmtList &s);
    /// @brief Analyze PRINT statement @p s.
    void analyzePrint(const PrintStmt &s);
    /// @brief Analyze PRINT # statement @p s.
    void analyzePrintCh(const PrintChStmt &s);
    /// @brief Analyze LET statement @p s.
    void analyzeLet(LetStmt &s);
    /// @brief Analyze OPEN statement @p s.
    void analyzeOpen(OpenStmt &s);
    /// @brief Analyze CLOSE statement @p s.
    void analyzeClose(CloseStmt &s);
    /// @brief Analyze IF statement @p s.
    void analyzeIf(const IfStmt &s);
    /// @brief Analyze WHILE statement @p s.
    void analyzeWhile(const WhileStmt &s);
    /// @brief Analyze DO statement @p s.
    void analyzeDo(const DoStmt &s);
    /// @brief Analyze FOR statement @p s.
    void analyzeFor(ForStmt &s);
    /// @brief Analyze GOTO statement @p s.
    void analyzeGoto(const GotoStmt &s);
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
    /// @brief Analyze LINE INPUT # statement @p s.
    void analyzeLineInputCh(LineInputChStmt &s);
    /// @brief Analyze DIM statement @p s.
    void analyzeDim(DimStmt &s);
    /// @brief Analyze REDIM statement @p s.
    void analyzeReDim(ReDimStmt &s);

    /// @brief Analyze assignment to a simple variable in LET.
    void analyzeVarAssignment(VarExpr &v, const LetStmt &s);
    /// @brief Analyze assignment to an array element in LET.
    void analyzeArrayAssignment(ArrayExpr &a, const LetStmt &s);
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
        Unknown
    };

    /// @brief Look up the tracked type for @p name when available.
    /// @param name Symbol whose type should be queried.
    /// @return Inferred type when recorded; std::nullopt otherwise.
    std::optional<Type> lookupVarType(const std::string &name) const;

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
        void noteArrayMutation(const std::string &name, std::optional<long long> previous);
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
            std::optional<long long> previous;
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
        Reference,      ///< Existing symbol use; do not declare or type.
        Definition,     ///< Implicit definition; apply suffix defaults if unset.
        InputTarget     ///< INPUT destination; force suffix-based defaults.
    };

    /// @brief Track active loop constructs for EXIT validation.
    enum class LoopKind
    {
        For,
        While,
        Do
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
    /// @brief Emit operand type mismatch diagnostic for binary expressions.
    void emitOperandTypeMismatch(const BinaryExpr &expr, std::string_view diagId);
    /// @brief Emit divide-by-zero diagnostic when appropriate.
    void emitDivideByZero(const BinaryExpr &expr);
    /// @brief Determine whether the RHS of @p expr is the integer literal 0.
    bool rhsIsLiteralZero(const BinaryExpr &expr) const;
    /// @brief Ensure operands are numeric (INT or FLOAT) when required.
    void validateNumericOperands(const BinaryExpr &expr,
                                 Type lhs,
                                 Type rhs,
                                 std::string_view diagId);
    /// @brief Validate division operands and detect divide-by-zero.
    void validateDivisionOperands(const BinaryExpr &expr,
                                  Type lhs,
                                  Type rhs,
                                  std::string_view diagId);
    /// @brief Validate integer-only operators and detect divide-by-zero.
    void validateIntegerOperands(const BinaryExpr &expr,
                                 Type lhs,
                                 Type rhs,
                                 std::string_view diagId);
    /// @brief Validate comparison operands allowing numeric or string equality.
    void validateComparisonOperands(const BinaryExpr &expr,
                                    Type lhs,
                                    Type rhs,
                                    std::string_view diagId);
    /// @brief Validate logical operators requiring BOOLEAN operands.
    void validateLogicalOperands(const BinaryExpr &expr,
                                 Type lhs,
                                 Type rhs,
                                 std::string_view diagId);
    /// @brief Analyze built-in function call.
    Type analyzeBuiltinCall(const BuiltinCallExpr &c);

  public:
    /// @brief Metadata describing a single builtin argument slot.
    struct BuiltinArgSpec
    {
        bool optional{false}; ///< Whether the argument may be omitted.
        const Type *allowed{nullptr}; ///< Pointer to allowed types array.
        std::size_t allowedCount{0};  ///< Number of entries in @ref allowed.
    };

    /// @brief Metadata describing builtin arity and result type.
    struct BuiltinSignature
    {
        std::size_t requiredArgs{0}; ///< Number of mandatory arguments.
        std::size_t optionalArgs{0}; ///< Number of optional arguments.
        const BuiltinArgSpec *arguments{nullptr}; ///< Per-position argument specs.
        std::size_t argumentCount{0}; ///< Total number of entries in @ref arguments.
        Type result{Type::Unknown};   ///< Result type reported when checks pass.
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
    const ProcSignature *resolveCallee(const CallExpr &c);
    /// @brief Collect and validate argument types for user-defined call.
    std::vector<Type> checkCallArgs(const CallExpr &c, const ProcSignature *sig);
    /// @brief Infer return type for user-defined call.
    Type inferCallType(const CallExpr &c, const ProcSignature *sig);
    /// @brief Analyze user-defined procedure call.
    Type analyzeCall(const CallExpr &c);
    /// @brief Analyze array access expression.
    Type analyzeArray(ArrayExpr &a);

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

    /// @brief Shared setup/teardown for analyzing procedures.
    template <typename Proc, typename BodyCallback>
    void analyzeProcedureCommon(const Proc &proc, BodyCallback &&bodyCheck);

    /// @brief Register parameter @p param in the current procedure scope.
    void registerProcedureParam(const Param &param);

    /// @brief Analyze body of FUNCTION @p f.
    void analyzeProc(const FunctionDecl &f);
    /// @brief Analyze body of SUB @p s.
    void analyzeProc(const SubDecl &s);

    SemanticDiagnostics de; ///< Diagnostic sink.
    ScopeTracker scopes_;
    ProcRegistry procReg_;
    std::unordered_set<std::string> symbols_;
    std::unordered_map<std::string, Type> varTypes_;
    std::unordered_map<std::string, long long> arrays_; ///< array sizes if known (-1 if dynamic)
    std::unordered_set<long long> openChannels_; ///< Channels opened by literal handles.
    std::unordered_set<int> labels_;
    std::unordered_set<int> labelRefs_;
    std::vector<std::string> forStack_; ///< Active FOR loop variables.
    std::vector<LoopKind> loopStack_;   ///< Active loop constructs for EXIT validation.
    ProcedureScope *activeProcScope_{nullptr};
    bool errorHandlerActive_{false};
    std::optional<int> errorHandlerTarget_;
};

} // namespace il::frontends::basic
