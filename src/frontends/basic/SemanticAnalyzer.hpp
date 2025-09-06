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

    Type retType{Type::I64}; ///< Return type for FUNCTION; ignored for SUB.

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

    /// @brief Inferred BASIC value type.
    enum class Type
    {
        Int,
        Float,
        String,
        Unknown
    };

    /// @brief Validate variable references in @p e and recurse into subtrees.
    /// @param e Expression node to analyze.
    /// @return Inferred type of the expression.
    Type visitExpr(const Expr &e);

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
