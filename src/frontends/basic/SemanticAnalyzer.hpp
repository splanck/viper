// File: src/frontends/basic/SemanticAnalyzer.hpp
// Purpose: Declares BASIC semantic analyzer for symbol and label tracking,
//          basic validation, and two-pass procedure registration.
// Key invariants: Analyzer tracks defined symbols and reports unknown
//                 references.
// Ownership/Lifetime: Analyzer borrows DiagnosticEmitter; no AST ownership.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include <initializer_list>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::frontends::basic
{

/// @brief Signature information for a declared procedure.
struct ProcSignature
{
    /// @brief Procedure kind distinguishing FUNCTION from SUB.
    enum class Kind
    {
        Function,
        Sub
    } kind{Kind::Function};

    std::optional<Type> retType; ///< Return type for FUNCTION; nullopt for SUB.

    /// @brief Parameter type descriptor.
    struct Param
    {
        Type type{Type::I64}; ///< Parameter BASIC type.
        bool is_array{false}; ///< True if parameter declared with ().
    };

    std::vector<Param> params; ///< Ordered parameter types.
};

/// @brief Table mapping procedure name to its signature.
using ProcTable = std::unordered_map<std::string, ProcSignature>;

/// @brief Traverses BASIC AST to collect symbols and labels, validate variable
///        references, and verify FOR/NEXT nesting.
/// @invariant Symbol table only contains definitions; unknown uses report
///            diagnostics.
/// @ownership Borrows DiagnosticEmitter; AST not owned.
class SemanticAnalyzer
{
  public:
    /// @brief Create analyzer reporting to @p de.
    explicit SemanticAnalyzer(DiagnosticEmitter &de) : de(de) {}

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
        return procs_;
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
        Unknown
    };

  private:
    /// @brief Validate variable references in @p e and recurse into subtrees.
    /// @param e Expression node to analyze.
    /// @return Inferred type of the expression.
    Type visitExpr(const Expr &e);

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

  public:
    /// @brief Analyze RND builtin.
    Type analyzeRnd(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze LEN builtin.
    Type analyzeLen(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze MID$ builtin.
    Type analyzeMid(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze LEFT$ builtin.
    Type analyzeLeft(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze RIGHT$ builtin.
    Type analyzeRight(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze STR$ builtin.
    Type analyzeStr(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze VAL builtin.
    Type analyzeVal(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze INT builtin.
    Type analyzeInt(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze INSTR builtin.
    Type analyzeInstr(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze LTRIM$ builtin.
    Type analyzeLtrim(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze RTRIM$ builtin.
    Type analyzeRtrim(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze TRIM$ builtin.
    Type analyzeTrim(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze UCASE$ builtin.
    Type analyzeUcase(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze LCASE$ builtin.
    Type analyzeLcase(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze CHR$ builtin.
    Type analyzeChr(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze ASC builtin.
    Type analyzeAsc(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze SQR builtin.
    Type analyzeSqr(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze ABS builtin.
    Type analyzeAbs(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze FLOOR builtin.
    Type analyzeFloor(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze CEIL builtin.
    Type analyzeCeil(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze SIN builtin.
    Type analyzeSin(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze COS builtin.
    Type analyzeCos(const BuiltinCallExpr &c, const std::vector<Type> &args);
    /// @brief Analyze POW builtin.
    Type analyzePow(const BuiltinCallExpr &c, const std::vector<Type> &args);

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
                      std::initializer_list<Type> allowed);
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

    DiagnosticEmitter &de; ///< Diagnostic sink.
    std::unordered_set<std::string> symbols_;
    std::unordered_map<std::string, Type> varTypes_;
    std::unordered_map<std::string, long long> arrays_; ///< array sizes if known (-1 if dynamic)
    std::unordered_set<int> labels_;
    std::unordered_set<int> labelRefs_;
    std::vector<std::string> forStack_; ///< Active FOR loop variables.
    ProcTable procs_;                   ///< Registered procedures.

    // scope management
    std::vector<std::unordered_map<std::string, std::string>>
        scopeStack_;          ///< @brief Stack of scopes mapping original names to mangled.
    unsigned nextLocalId_{0}; ///< @brief Counter for unique local names.

    /// @brief RAII helper entering a scope on construction and leaving on destruction.
    class ScopedScope
    {
      public:
        /// @brief Construct and push a new scope on @p sa.
        explicit ScopedScope(SemanticAnalyzer &sa);
        /// @brief Pop the managed scope.
        ~ScopedScope();

      private:
        SemanticAnalyzer &sa_;
    };

    /// @brief Enter a new lexical scope.
    void pushScope();
    /// @brief Exit the current lexical scope.
    void popScope();
    /// @brief Resolve @p name, returning mangled form if found.
    std::optional<std::string> resolve(const std::string &name) const;

    /// @brief Register FUNCTION declaration @p f in the procedure table.
    void registerProc(const FunctionDecl &f);
    /// @brief Register SUB declaration @p s in the procedure table.
    void registerProc(const SubDecl &s);
    /// @brief Analyze body of FUNCTION @p f.
    void analyzeProc(const FunctionDecl &f);
    /// @brief Analyze body of SUB @p s.
    void analyzeProc(const SubDecl &s);
};

} // namespace il::frontends::basic
