//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/repl/ZiaReplAdapter.hpp
// Purpose: Zia-specific REPL adapter implementing the ReplAdapter interface.
//          Manages compilation of REPL inputs, session state tracking
//          (variables, functions, binds), and BytecodeVM execution.
// Key invariants:
//   - Each input compiles to a fresh IL Module.
//   - Session variables persist across inputs via rt_modvar.
//   - Failed compilations do not modify session state.
// Ownership/Lifetime:
//   - Owns accumulated source fragments and the SourceManager.
//   - BytecodeVM is created per-eval (shared RtContext provides persistence).
// Links: src/repl/ReplSession.hpp, src/frontends/zia/Compiler.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ReplSession.hpp"

#include "frontends/zia/ZiaCompletion.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace viper::repl {

/// @brief Zia language REPL adapter.
/// @details Compiles each REPL input by building a synthetic Zia source file
///          from accumulated state (binds, types, functions, global var declarations)
///          plus the current input wrapped in func start() { ... }.
/// @brief Tracked persistent variable for cross-input replay.
struct PersistentVar {
    std::string name;           ///< Variable name.
    std::string declStatement;  ///< Full declaration (e.g., "var x = 42").
    std::string lastAssignment; ///< Latest reassignment (e.g., "x = 100"), or empty.
    std::string type;           ///< Inferred or annotated type (e.g., "Integer").
};

class ZiaReplAdapter : public ReplAdapter {
  public:
    ZiaReplAdapter();
    ~ZiaReplAdapter() override = default;

    EvalResult eval(const std::string &input) override;
    std::vector<std::string> complete(const std::string &input, size_t cursor) override;
    std::vector<VarInfo> listVariables() const override;
    std::vector<FuncInfo> listFunctions() const override;
    std::vector<std::string> listBinds() const override;
    std::string_view languageName() const override;
    std::string getExprType(const std::string &expr) override;
    std::string getIL(const std::string &input) override;
    void reset() override;

  private:
    /// @brief Build the full synthetic Zia source for compilation.
    /// @param input The current REPL input to wrap in start().
    /// @param extraTopLevel Optional declaration emitted after persisted globals.
    /// @return The complete Zia program source.
    std::string buildSource(const std::string &input,
                            const std::string &extraTopLevel = std::string()) const;

    /// @brief Try to compile source without execution (for expression type probing).
    /// @return True if compilation succeeded.
    bool tryCompileOnly(const std::string &source) const;

    /// @brief Compile and verify source only to produce a diagnostic string.
    /// @details Used after a validation probe fails so callers can surface the
    ///          verifier error as well as frontend diagnostics.
    /// @param source Complete synthetic Zia source to compile.
    /// @return Human-readable diagnostics, or an empty string if source is valid.
    std::string compileOnlyDiagnostic(const std::string &source) const;

    /// @brief Check if input looks like a bind statement.
    bool isBind(const std::string &input) const;

    /// @brief Check if input looks like a function definition.
    bool isFuncDef(const std::string &input) const;

    /// @brief Check if input looks like an class/struct/interface definition.
    bool isTypeDef(const std::string &input) const;

    /// @brief Check if input looks like a variable declaration.
    bool isVarDecl(const std::string &input) const;

    /// @brief Check if input looks like it could be an expression (for auto-print).
    bool isLikelyExpression(const std::string &input) const;

    /// @brief Check if input assigns to a known persistent variable root.
    bool isAssignment(const std::string &input) const;

    /// @brief Extract the target variable name from an assignment.
    std::string extractAssignTarget(const std::string &input) const;

    /// @brief Extract function name from a func definition.
    std::string extractFuncName(const std::string &input) const;

    /// @brief Extract type name from an class/struct/interface definition.
    std::string extractTypeName(const std::string &input) const;

    /// @brief Extract variable name and type from a var declaration.
    std::pair<std::string, std::string> extractVarInfo(const std::string &input) const;

    /// @brief Extract the initializer expression from a variable declaration.
    /// @param input Full source text of a Zia `var` declaration.
    /// @return Initializer expression without trailing semicolon, or empty when absent.
    std::string extractVarInitializer(const std::string &input) const;

    /// @brief Infer a storable declaration type for a REPL variable.
    /// @details Prefer an explicit annotation, then common constructor syntax,
    ///          then the semantic probe used by `.type`. Returns `auto` when no
    ///          stable source-level type can be inferred.
    /// @param input Full source text of a Zia `var` declaration.
    /// @param explicitType Type extracted from the declaration, or `auto`.
    /// @param initializer Initializer expression extracted from the declaration.
    /// @return Source-level type name suitable for a top-level declaration.
    std::string inferPersistentVarType(const std::string &input,
                                       const std::string &explicitType,
                                       const std::string &initializer);

    /// @brief Build a top-level declaration for a persisted REPL variable.
    /// @param name Variable name.
    /// @param type Source-level variable type.
    /// @return Declaration text without a trailing newline.
    std::string makePersistentVarDecl(const std::string &name, const std::string &type) const;

    /// @brief Find a persistent variable by name.
    PersistentVar *findPersistentVar(const std::string &name);
    const PersistentVar *findPersistentVar(const std::string &name) const;

    /// @brief Compile and execute the given source, capturing stdout.
    /// @return EvalResult with output or error.
    EvalResult compileAndRun(const std::string &source);

    /// @brief Build source for completion and calculate cursor position.
    /// @param input The current line buffer.
    /// @param cursor Cursor position in the input.
    /// @param[out] line 1-based line number in synthetic source.
    /// @param[out] col 0-based column in synthetic source.
    /// @return The synthetic source text.
    std::string buildSourceForCompletion(const std::string &input,
                                         size_t cursor,
                                         int &line,
                                         int &col) const;

    // --- Session state ---
    std::vector<std::string> bindStatements_;
    std::map<std::string, std::string> definedFunctions_; ///< name -> full source
    std::map<std::string, std::string> definedTypes_;     ///< name -> full source
    std::vector<PersistentVar> persistentVars_;           ///< ordered persistent variables
    std::map<std::string, std::string> globalVarDecls_;   ///< name -> type (for completion)

    // --- Completion engine ---
    il::frontends::zia::CompletionEngine completionEngine_;
};

} // namespace viper::repl
