//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/repl/BasicReplAdapter.hpp
// Purpose: BASIC-specific REPL adapter implementing the ReplAdapter interface.
//          Manages compilation of REPL inputs, session state tracking
//          (variables, subroutines), and BytecodeVM execution.
// Key invariants:
//   - Each input compiles to a fresh IL Module.
//   - Session variables persist across inputs via DIM replay.
//   - Failed compilations do not modify session state.
//   - Uses classifyBasic() for multi-line block keyword tracking.
// Ownership/Lifetime:
//   - Owns accumulated source fragments and the SourceManager.
//   - BytecodeVM is created per-eval.
// Links: src/repl/ReplSession.hpp, src/frontends/basic/BasicCompiler.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ReplSession.hpp"

#include <map>
#include <string>
#include <vector>

namespace viper::repl {

/// @brief Tracked persistent BASIC variable for cross-input replay.
struct BasicPersistentVar {
    std::string name;       ///< Variable name.
    std::string type;       ///< BASIC type (e.g., "Integer", "String").
    std::string dimStmt;    ///< Full DIM statement (e.g., "DIM x AS Integer = 42").
    std::string lastAssign; ///< Latest reassignment (e.g., "x = 100"), or empty.
};

/// @brief BASIC language REPL adapter.
/// @details Compiles each REPL input by building a synthetic BASIC source file
///          from accumulated state (DIM declarations, SUB/FUNCTION definitions)
///          plus the current input as top-level code.
class BasicReplAdapter : public ReplAdapter {
  public:
    BasicReplAdapter();
    ~BasicReplAdapter() override = default;

    EvalResult eval(const std::string &input) override;
    std::vector<std::string> complete(const std::string &input, size_t cursor) override;
    std::vector<VarInfo> listVariables() const override;
    std::vector<FuncInfo> listFunctions() const override;
    std::vector<std::string> listBinds() const override;
    std::string_view languageName() const override;
    InputKind classifyInput(const std::string &input) override;
    void reset() override;

  private:
    /// @brief Build the full synthetic BASIC source for compilation.
    /// @param input The current REPL input to include as top-level code.
    /// @return The complete BASIC program source.
    std::string buildSource(const std::string &input) const;

    /// @brief Try to compile source without execution (for expression type probing).
    /// @return True if compilation succeeded.
    bool tryCompileOnly(const std::string &source) const;

    /// @brief Check if input looks like a SUB or FUNCTION definition.
    bool isSubOrFunc(const std::string &input) const;

    /// @brief Check if input looks like a variable declaration (DIM).
    bool isDimDecl(const std::string &input) const;

    /// @brief Check if input is a bare assignment to a known variable.
    bool isAssignment(const std::string &input) const;

    /// @brief Check if input could be an expression (for auto-print).
    bool isLikelyExpression(const std::string &input) const;

    /// @brief Extract SUB/FUNCTION name from a definition.
    std::string extractProcName(const std::string &input) const;

    /// @brief Extract variable name and type from a DIM declaration.
    std::pair<std::string, std::string> extractDimInfo(const std::string &input) const;

    /// @brief Extract assignment target name.
    std::string extractAssignTarget(const std::string &input) const;

    /// @brief Find a persistent variable by name.
    BasicPersistentVar *findPersistentVar(const std::string &name);
    const BasicPersistentVar *findPersistentVar(const std::string &name) const;

    /// @brief Compile and execute the given source, capturing stdout.
    EvalResult compileAndRun(const std::string &source);

    // --- Session state ---
    std::map<std::string, std::string> definedProcs_; ///< name -> full source (SUB/FUNCTION)
    std::vector<BasicPersistentVar> persistentVars_;  ///< ordered persistent variables
};

} // namespace viper::repl
