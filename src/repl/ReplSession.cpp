//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/repl/ReplSession.cpp
// Purpose: Implementation of the core REPL session loop.
// Key invariants:
//   - Compilation errors never destroy session state.
//   - Multi-line input accumulates until bracket depth reaches zero.
//   - Meta-commands are dispatched before any compilation attempt.
// Ownership/Lifetime:
//   - Owns the adapter, editor, and meta-command registry.
// Links: src/repl/ReplSession.hpp
//
//===----------------------------------------------------------------------===//

#include "ReplSession.hpp"
#include "ReplColorScheme.hpp"

#include "viper/version.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace viper::repl {

ReplSession::ReplSession(std::unique_ptr<ReplAdapter> adapter) : adapter_(std::move(adapter)) {
    registerDefaultCommands();

    // Wire up tab completion
    editor_.setCompletionCallback(
        [this](const std::string &input, size_t cursor) -> std::vector<std::string> {
            return adapter_->complete(input, cursor);
        });

    // Load persistent history
    auto histPath = historyFilePath();
    if (!histPath.empty())
        editor_.loadHistory(histPath);
}

std::filesystem::path ReplSession::historyFilePath() const {
    const char *home = std::getenv("HOME");
#ifdef _WIN32
    if (!home)
        home = std::getenv("USERPROFILE");
#endif
    if (!home)
        return {};

    std::filesystem::path dir(home);
    dir /= ".viper";
    std::string filename = "repl_history_";
    filename += adapter_->languageName();
    return dir / filename;
}

void ReplSession::registerDefaultCommands() {
    metaCmds_.registerCommand(
        "help",
        "Show this help message",
        [this](ReplSession & /*session*/, const std::string & /*args*/) { metaCmds_.printHelp(); });

    metaCmds_.registerCommand(
        "quit", "Exit the REPL", [](ReplSession &session, const std::string & /*args*/) {
            session.requestExit();
        });

    metaCmds_.registerCommand(
        "exit", "Exit the REPL", [](ReplSession &session, const std::string & /*args*/) {
            session.requestExit();
        });

    metaCmds_.registerCommand(
        "clear", "Reset session state", [](ReplSession &session, const std::string & /*args*/) {
            session.adapter().reset();
            std::cout << "Session state cleared.\n";
        });

    metaCmds_.registerCommand(
        "vars", "List session variables", [](ReplSession &session, const std::string & /*args*/) {
            auto vars = session.adapter().listVariables();
            if (vars.empty()) {
                std::cout << colors::dim() << "(no variables)" << colors::reset() << "\n";
                return;
            }
            for (const auto &v : vars) {
                std::cout << "  " << colors::bold() << v.name << colors::reset() << " : "
                          << colors::type() << v.type << colors::reset() << "\n";
            }
        });

    metaCmds_.registerCommand(
        "funcs", "List defined functions", [](ReplSession &session, const std::string & /*args*/) {
            auto funcs = session.adapter().listFunctions();
            if (funcs.empty()) {
                std::cout << colors::dim() << "(no functions)" << colors::reset() << "\n";
                return;
            }
            for (const auto &f : funcs) {
                std::cout << "  " << colors::bold() << f.name << colors::reset() << " "
                          << colors::dim() << f.signature << colors::reset() << "\n";
            }
        });

    metaCmds_.registerCommand("binds",
                              "List active bind statements",
                              [](ReplSession &session, const std::string & /*args*/) {
                                  auto binds = session.adapter().listBinds();
                                  if (binds.empty()) {
                                      std::cout << colors::dim() << "(no binds)" << colors::reset()
                                                << "\n";
                                      return;
                                  }
                                  for (const auto &b : binds) {
                                      std::cout << "  " << b << "\n";
                                  }
                              });

    metaCmds_.registerCommand(
        "type", "Show type of expression", [](ReplSession &session, const std::string &args) {
            if (args.empty()) {
                std::cout << colors::warning() << "Usage: .type <expression>" << colors::reset()
                          << "\n";
                return;
            }
            std::string typeStr = session.adapter().getExprType(args);
            std::cout << colors::type() << typeStr << colors::reset() << "\n";
        });

    metaCmds_.registerCommand("il",
                              "Show generated IL for expression",
                              [](ReplSession &session, const std::string &args) {
                                  if (args.empty()) {
                                      std::cout << colors::warning() << "Usage: .il <expression>"
                                                << colors::reset() << "\n";
                                      return;
                                  }
                                  std::string il = session.adapter().getIL(args);
                                  std::cout << colors::dim() << il << colors::reset();
                                  if (!il.empty() && il.back() != '\n')
                                      std::cout << "\n";
                              });

    metaCmds_.registerCommand(
        "time",
        "Evaluate and show execution time",
        [](ReplSession &session, const std::string &args) {
            if (args.empty()) {
                std::cout << colors::warning() << "Usage: .time <expression>" << colors::reset()
                          << "\n";
                return;
            }
            auto start = std::chrono::high_resolution_clock::now();
            auto result = session.adapter().eval(args);
            auto end = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

            if (result.success && !result.output.empty()) {
                std::cout << colors::result() << result.output << colors::reset();
                if (result.output.back() != '\n')
                    std::cout << "\n";
            } else if (!result.success) {
                std::cout << colors::error() << result.errorMessage << colors::reset();
                if (!result.errorMessage.empty() && result.errorMessage.back() != '\n')
                    std::cout << "\n";
            }

            // Format elapsed time
            if (elapsed.count() >= 1000000) {
                double secs = static_cast<double>(elapsed.count()) / 1000000.0;
                std::cout << colors::dim() << "Elapsed: " << secs << "s" << colors::reset() << "\n";
            } else if (elapsed.count() >= 1000) {
                double ms = static_cast<double>(elapsed.count()) / 1000.0;
                std::cout << colors::dim() << "Elapsed: " << ms << "ms" << colors::reset() << "\n";
            } else {
                std::cout << colors::dim() << "Elapsed: " << elapsed.count() << "us"
                          << colors::reset() << "\n";
            }
        });

    metaCmds_.registerCommand(
        "load",
        "Load and execute a source file",
        [](ReplSession &session, const std::string &args) {
            if (args.empty()) {
                std::cout << colors::warning() << "Usage: .load <filepath>" << colors::reset()
                          << "\n";
                return;
            }
            std::ifstream file(args);
            if (!file.is_open()) {
                std::cout << colors::error() << "Could not open: " << args << colors::reset()
                          << "\n";
                return;
            }
            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
            file.close();

            // Execute the file content as a single input
            auto result = session.adapter().eval(content);
            if (result.success) {
                if (!result.output.empty()) {
                    std::cout << colors::result() << result.output << colors::reset();
                    if (result.output.back() != '\n')
                        std::cout << "\n";
                }
                std::cout << colors::success() << "Loaded: " << args << colors::reset() << "\n";
            } else {
                std::cout << colors::error() << result.errorMessage << colors::reset();
                if (!result.errorMessage.empty() && result.errorMessage.back() != '\n')
                    std::cout << "\n";
            }
        });

    metaCmds_.registerCommand("save",
                              "Save session history to a file",
                              [this](ReplSession & /*session*/, const std::string &args) {
                                  if (args.empty()) {
                                      std::cout << colors::warning() << "Usage: .save <filepath>"
                                                << colors::reset() << "\n";
                                      return;
                                  }
                                  std::ofstream file(args);
                                  if (!file.is_open()) {
                                      std::cout << colors::error() << "Could not write: " << args
                                                << colors::reset() << "\n";
                                      return;
                                  }
                                  auto history = editor_.getHistory();
                                  for (const auto &entry : history) {
                                      file << entry << "\n";
                                  }
                                  file.close();
                                  std::cout << colors::success() << "Saved " << history.size()
                                            << " entries to: " << args << colors::reset() << "\n";
                              });
}

void ReplSession::printBanner() {
    std::cout << colors::bold() << "Viper " << adapter_->languageName() << " REPL"
              << colors::reset() << " v" << VIPER_VERSION_STR << "\n";
    std::cout << "Type " << colors::prompt() << ".help" << colors::reset() << " for commands, "
              << colors::prompt() << ".quit" << colors::reset() << " to exit.\n\n";
}

std::string ReplSession::makePrompt() const {
    std::string p;
    p += colors::prompt();
    p += adapter_->languageName();
    p += "> ";
    p += colors::reset();
    return p;
}

std::string ReplSession::makeContinuationPrompt() const {
    std::string p;
    p += colors::contPrompt();
    p += "...> ";
    p += colors::reset();
    return p;
}

void ReplSession::requestExit() {
    running_ = false;
}

int ReplSession::run() {
    const bool interactive = editor_.isActive();

    if (interactive)
        printBanner();

    while (running_) {
        std::string line;

        if (interactive) {
            // Interactive mode with line editing
            std::string prompt =
                accumulatedInput_.empty() ? makePrompt() : makeContinuationPrompt();
            ReadResult readResult = editor_.readLine(prompt, line);

            switch (readResult) {
                case ReadResult::Eof:
                    running_ = false;
                    continue;

                case ReadResult::Interrupt:
                    if (!accumulatedInput_.empty()) {
                        accumulatedInput_.clear();
                        consecutiveInterrupts_ = 0;
                        std::cout << colors::note() << "(input cancelled)" << colors::reset()
                                  << "\n";
                    } else {
                        ++consecutiveInterrupts_;
                        if (consecutiveInterrupts_ >= 2) {
                            running_ = false;
                        } else {
                            std::cout << colors::dim() << "(press Ctrl-C again to exit)"
                                      << colors::reset() << "\n";
                        }
                    }
                    continue;

                case ReadResult::Line:
                    consecutiveInterrupts_ = 0;
                    break;
            }
        } else {
            // Non-interactive mode (piped input): read lines from stdin
            if (!std::getline(std::cin, line)) {
                running_ = false;
                continue;
            }
        }

        // Accumulate input
        if (!accumulatedInput_.empty()) {
            accumulatedInput_ += "\n";
        }
        accumulatedInput_ += line;

        // Classify accumulated input
        InputKind kind = adapter_->classifyInput(accumulatedInput_);

        switch (kind) {
            case InputKind::Empty:
                accumulatedInput_.clear();
                continue;

            case InputKind::MetaCommand:
                metaCmds_.tryHandle(accumulatedInput_, *this);
                editor_.addHistory(accumulatedInput_);
                accumulatedInput_.clear();
                continue;

            case InputKind::Incomplete:
                // Need more input; loop with continuation prompt
                continue;

            case InputKind::Complete:
                break;
        }

        // Evaluate the complete input
        ++inputCounter_;
        editor_.addHistory(accumulatedInput_);

        EvalResult result = adapter_->eval(accumulatedInput_);
        accumulatedInput_.clear();

        if (result.success) {
            if (!result.output.empty()) {
                // Choose color based on expression result type
                const char *color;
                switch (result.resultType) {
                    case ResultType::Integer:
                    case ResultType::Number:
                        color = colors::number();
                        break;
                    case ResultType::String:
                        color = colors::string();
                        break;
                    case ResultType::Boolean:
                        color = colors::boolean();
                        break;
                    case ResultType::Object:
                        color = colors::type();
                        break;
                    default:
                        color = colors::result();
                        break;
                }
                std::cout << color << result.output << colors::reset();
                // Ensure trailing newline
                if (result.output.back() != '\n')
                    std::cout << "\n";
            }
        } else {
            std::cout << colors::error() << result.errorMessage << colors::reset();
            if (!result.errorMessage.empty() && result.errorMessage.back() != '\n')
                std::cout << "\n";
        }
    }

    // Save history to persistent file
    auto histPath = historyFilePath();
    if (!histPath.empty())
        editor_.saveHistory(histPath);

    std::cout << "Goodbye.\n";
    return 0;
}

} // namespace viper::repl
