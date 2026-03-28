//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/repl/ReplSession.hpp
// Purpose: Core REPL session state machine. Orchestrates the READ-EVAL-PRINT
//          loop using a language-specific ReplAdapter, the custom line editor,
//          input classifier, meta-command dispatcher, and pretty printer.
// Key invariants:
//   - The session loop runs until .quit, Ctrl-D, or an unrecoverable error.
//   - Failed compilations do not destroy session state.
//   - The adapter is responsible for language-specific compilation.
// Ownership/Lifetime:
//   - Owns the ReplAdapter, ReplLineEditor, and ReplMetaCommands.
//   - The adapter outlives all compilation calls.
// Links: src/repl/ReplLineEditor.hpp, src/repl/ReplMetaCommands.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ReplInputClassifier.hpp"
#include "ReplLineEditor.hpp"
#include "ReplMetaCommands.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace viper::repl {

/// @brief Information about a session variable.
struct VarInfo {
    std::string name;
    std::string type;
};

/// @brief Information about a session function.
struct FuncInfo {
    std::string name;
    std::string signature;
};

/// @brief The detected type of a REPL expression result.
enum class ResultType {
    None,      ///< No printable result (declaration, void call, etc.).
    Statement, ///< Statement that produced output via explicit Say/Print.
    Integer,   ///< Expression auto-printed as Integer.
    Number,    ///< Expression auto-printed as Number.
    String,    ///< Expression auto-printed as String.
    Boolean,   ///< Expression auto-printed as Boolean.
    Object,    ///< Expression auto-printed as Object (class/struct type).
};

/// @brief Result of evaluating a REPL input.
struct EvalResult {
    bool success{false};                     ///< True if compilation and execution succeeded.
    std::string output;                      ///< Captured stdout from execution.
    std::string errorMessage;                ///< Error message on failure.
    bool trapped{false};                     ///< True if the VM trapped during execution.
    ResultType resultType{ResultType::None}; ///< Type of auto-printed result.
};

/// @brief Language-specific REPL adapter interface.
/// @details Implementations provide compilation, execution, completion, and
///          session state tracking for a specific language (Zia or BASIC).
class ReplAdapter {
  public:
    virtual ~ReplAdapter() = default;

    /// @brief Compile and execute REPL input.
    /// @param input The user's input (may be a statement, expression, or declaration).
    /// @return Result of compilation and execution.
    virtual EvalResult eval(const std::string &input) = 0;

    /// @brief Provide tab completions for the current input.
    /// @param input Current line buffer content.
    /// @param cursor Cursor position in the buffer.
    /// @return Vector of completion strings.
    virtual std::vector<std::string> complete(const std::string &input, size_t cursor) = 0;

    /// @brief List all session variables.
    virtual std::vector<VarInfo> listVariables() const = 0;

    /// @brief List all user-defined functions.
    virtual std::vector<FuncInfo> listFunctions() const = 0;

    /// @brief List all active bind statements.
    virtual std::vector<std::string> listBinds() const = 0;

    /// @brief Get the language name (for prompts and messages).
    virtual std::string_view languageName() const = 0;

    /// @brief Get the inferred type of an expression (for .type command).
    /// @param expr The expression to analyze.
    /// @return Human-readable type string, or error message.
    virtual std::string getExprType(const std::string &expr) {
        (void)expr;
        return "not supported";
    }

    /// @brief Get the generated IL for an expression or statement (for .il command).
    /// @param input The code to compile.
    /// @return IL text or error message.
    virtual std::string getIL(const std::string &input) {
        (void)input;
        return "not supported";
    }

    /// @brief Classify accumulated input for multi-line detection.
    /// @details Override to use language-specific classification (e.g., BASIC block keywords).
    ///          Default uses Zia-style bracket depth tracking.
    /// @param input The accumulated REPL input.
    /// @return Classification of the input.
    virtual InputKind classifyInput(const std::string &input) {
        return ReplInputClassifier::classify(input);
    }

    /// @brief Reset all session state.
    virtual void reset() = 0;
};

/// @brief Core REPL session managing the read-eval-print loop.
/// @details Orchestrates line editing, input classification, meta-command
///          dispatch, and language-specific evaluation via a ReplAdapter.
class ReplSession {
  public:
    /// @brief Construct a REPL session with the given language adapter.
    /// @param adapter Language-specific REPL adapter (takes ownership).
    explicit ReplSession(std::unique_ptr<ReplAdapter> adapter);

    /// @brief Run the REPL loop until exit.
    /// @return Exit code (0 on clean exit).
    int run();

    /// @brief Request the REPL to exit on the next iteration.
    void requestExit();

    /// @brief Get the language adapter.
    ReplAdapter &adapter() {
        return *adapter_;
    }

  private:
    void registerDefaultCommands();
    void printBanner();
    std::string makePrompt() const;
    std::string makeContinuationPrompt() const;

    /// @brief Get the history file path for the current language.
    /// @return Path like ~/.viper/repl_history_zia or ~/.viper/repl_history_basic.
    std::filesystem::path historyFilePath() const;

    std::unique_ptr<ReplAdapter> adapter_;
    ReplLineEditor editor_;
    ReplMetaCommands metaCmds_;

    std::string accumulatedInput_;
    int inputCounter_{0};
    bool running_{true};
    int consecutiveInterrupts_{0}; ///< Tracks consecutive Ctrl-C on empty line.
};

} // namespace viper::repl
