// File: src/frontends/basic/SemanticAnalyzer.hpp
// Purpose: Declares BASIC semantic analyzer for symbol and label tracking,
//          basic validation, and two-pass procedure registration.
// Key invariants: Analyzer tracks defined symbols and reports unknown
//                 references.
// Ownership/Lifetime: Analyzer borrows DiagnosticEmitter; no AST ownership.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/ProcRegistry.hpp"
#include "frontends/basic/ScopeTracker.hpp"
#include "frontends/basic/SemanticDiagnostics.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::frontends::basic
{

struct BuiltinInfo;
struct BuiltinSignature;

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
    explicit SemanticAnalyzer(DiagnosticEmitter &emitter) : de(emitter), procReg_(de) {}

    /// @brief Analyze @p prog collecting symbols and labels.
    /// @param prog Program AST to walk.
    void analyze(const Program &prog);

    /// @brief Collected variable names defined in the program.
    const std::unordered_set<std::string> &symbols() const
    {
        return symbols_;
    }

    /// @brief Line numbers present in the program.
    const std::unordered_set<int> &labels() const
    {
        return labels_;
    }

    /// @brief GOTO targets referenced in the program.
    const std::unordered_set<int> &labelRefs() const
    {
        return labelRefs_;
    }

    /// @brief Registered procedures and their signatures.
    const ProcTable &procs() const
    {
        return procReg_.procs();
    }

  private:
    /// @brief Record symbols and labels from a statement.
    /// @param s Statement node to analyze.
    void visitStmt(const Stmt &s);

    /// @brief Analyze statement list @p s.
    void analyzeStmtList(const StmtList &s);
    /// @brief Analyze PRINT statement @p s.
    void analyzePrint(const PrintStmt &s);
    /// @brief Analyze LET statement @p s.
    void analyzeLet(const LetStmt &s);
    /// @brief Analyze IF statement @p s.
    void analyzeIf(const IfStmt &s);
    /// @brief Analyze WHILE statement @p s.
    void analyzeWhile(const WhileStmt &s);
    /// @brief Analyze FOR statement @p s.
    void analyzeFor(const ForStmt &s);
    /// @brief Analyze GOTO statement @p s.
    void analyzeGoto(const GotoStmt &s);
    /// @brief Analyze NEXT statement @p s.
    void analyzeNext(const NextStmt &s);
    /// @brief Analyze END statement @p s.
    void analyzeEnd(const EndStmt &s);
    /// @brief Analyze RANDOMIZE statement @p s.
    void analyzeRandomize(const RandomizeStmt &s);
    /// @brief Analyze INPUT statement @p s.
    void analyzeInput(const InputStmt &s);
    /// @brief Analyze DIM statement @p s.
    void analyzeDim(const DimStmt &s);

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
        Unknown
    };

  private:
    class ProcedureScope
    {
      public:
        explicit ProcedureScope(SemanticAnalyzer &analyzer) noexcept
            : analyzer_(analyzer),
              savedSymbols_(analyzer.symbols_),
              savedVarTypes_(analyzer.varTypes_),
              savedArrays_(analyzer.arrays_),
              savedLabels_(analyzer.labels_),
              savedLabelRefs_(analyzer.labelRefs_),
              savedForStack_(analyzer.forStack_)
        {
            analyzer_.scopes_.pushScope();
        }

        ProcedureScope(const ProcedureScope &) = delete;
        ProcedureScope &operator=(const ProcedureScope &) = delete;

        ~ProcedureScope() noexcept
        {
            analyzer_.scopes_.popScope();
            analyzer_.symbols_ = std::move(savedSymbols_);
            analyzer_.varTypes_ = std::move(savedVarTypes_);
            analyzer_.arrays_ = std::move(savedArrays_);
            analyzer_.labels_ = std::move(savedLabels_);
            analyzer_.labelRefs_ = std::move(savedLabelRefs_);
            analyzer_.forStack_ = std::move(savedForStack_);
        }

      private:
        SemanticAnalyzer &analyzer_;
        std::unordered_set<std::string> savedSymbols_;
        std::unordered_map<std::string, Type> savedVarTypes_;
        std::unordered_map<std::string, long long> savedArrays_;
        std::unordered_set<int> savedLabels_;
        std::unordered_set<int> savedLabelRefs_;
        std::vector<std::string> savedForStack_;
    };

    /// @brief Validate variable references in @p e and recurse into subtrees.
    /// @param e Expression node to analyze.
    /// @return Inferred type of the expression.
    Type visitExpr(const Expr &e);

    /// @brief Ensure a conditional expression yields a BOOLEAN result.
    /// @param expr Condition expression to analyze.
    void checkConditionExpr(const Expr &expr);

    /// @brief Analyze variable reference.
    Type analyzeVar(VarExpr &v);
    /// @brief Analyze unary expression.
    Type analyzeUnary(const UnaryExpr &u);
    /// @brief Analyze binary expression.
    Type analyzeBinary(const BinaryExpr &b);
    /// @brief Analyze arithmetic operators (+, -, *).
    Type analyzeArithmetic(const BinaryExpr &b, Type lt, Type rt);
    /// @brief Analyze division and modulus operators.
    Type analyzeDivMod(const BinaryExpr &b, Type lt, Type rt);
    /// @brief Analyze comparison operators (==, <>, <, <=, >, >=).
    Type analyzeComparison(const BinaryExpr &b, Type lt, Type rt);
    /// @brief Analyze logical operators (AND, OR).
    Type analyzeLogical(const BinaryExpr &b, Type lt, Type rt);
    /// @brief Analyze built-in function call.
    Type analyzeBuiltinCall(const BuiltinCallExpr &c);

  private:
    /// @brief Check builtin argument count using registry metadata.
    bool checkBuiltinArgCount(const BuiltinCallExpr &c,
                              const BuiltinInfo &info,
                              size_t actualCount);

    /// @brief Verify argument @p idx against the signature metadata.
    bool checkBuiltinArgType(const BuiltinCallExpr &c,
                             const BuiltinSignature &sig,
                             size_t idx,
                             Type argTy);

    /// @brief Infer builtin result type from registry metadata.
    Type inferBuiltinResult(const BuiltinInfo &info, const std::vector<Type> &args) const;

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

    /// @brief Shared setup/teardown for analyzing procedures.
    template <typename Proc, typename BodyCallback>
    void analyzeProcedureCommon(const Proc &proc, BodyCallback &&bodyCheck);

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
    std::unordered_set<int> labels_;
    std::unordered_set<int> labelRefs_;
    std::vector<std::string> forStack_; ///< Active FOR loop variables.
};

} // namespace il::frontends::basic
