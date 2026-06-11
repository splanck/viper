//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/viper/cmd_eval.cpp
// Purpose: Entry point for the `viper eval` subcommand. Evaluates a single
//          Zia or BASIC snippet through the REPL adapters and reports the
//          result on stdout, optionally as structured JSON.
// Key invariants:
//   - The snippet is evaluated as one REPL input with a fresh session.
//   - Exit codes are stable: 0 success, 1 usage error, 2 compile/eval error,
//     3 runtime trap.
//   - JSON output is a single object on stdout; diagnostics stay on stderr.
// Ownership/Lifetime:
//   - The language adapter lives for the duration of the command.
// Links: src/repl/ReplSession.hpp, src/repl/ZiaReplAdapter.hpp,
//        src/repl/BasicReplAdapter.hpp, docs/tools.md
//
//===----------------------------------------------------------------------===//

#include "repl/BasicReplAdapter.hpp"
#include "repl/ReplSession.hpp"
#include "repl/ZiaReplAdapter.hpp"
#include "support/diag_expected.hpp"

#include <cstring>
#include <iostream>
#include <iterator>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>

namespace {

/// @brief Print usage for `viper eval` to the given stream.
void printEvalUsage(std::ostream &os) {
    os << "Usage: viper eval [options] [code]\n"
       << "\n"
       << "Evaluate a single Zia or BASIC snippet and print the result.\n"
       << "Reads the snippet from stdin when no code argument is given.\n"
       << "\n"
       << "Options:\n"
       << "  --lang zia|basic   Snippet language (default: zia)\n"
       << "  --json             Emit a structured JSON result object on stdout\n"
       << "  --type             Include the inferred expression type (Zia only)\n"
       << "  --il               Include the generated IL (Zia only)\n"
       << "  -h, --help         Show this help\n"
       << "\n"
       << "Exit codes:\n"
       << "  0  evaluation succeeded\n"
       << "  1  usage error\n"
       << "  2  compile or evaluation error\n"
       << "  3  runtime trap\n"
       << "\n"
       << "Examples:\n"
       << "  viper eval '2 + 3 * 4'\n"
       << "  viper eval --json --type 'Viper.Math.Sqrt(2.0)'\n"
       << "  echo 'Say(\"hi\")' | viper eval\n";
}

/// @brief Map a REPL result type to its stable JSON string name.
std::string_view resultTypeName(viper::repl::ResultType type) {
    switch (type) {
        case viper::repl::ResultType::None:
            return "none";
        case viper::repl::ResultType::Statement:
            return "statement";
        case viper::repl::ResultType::Integer:
            return "Integer";
        case viper::repl::ResultType::Number:
            return "Number";
        case viper::repl::ResultType::String:
            return "String";
        case viper::repl::ResultType::Boolean:
            return "Boolean";
        case viper::repl::ResultType::Object:
            return "Object";
    }
    return "none";
}

/// @brief Emit a string as a JSON-escaped, double-quoted literal.
void printJsonString(std::ostream &os, std::string_view text) {
    il::support::printJsonStringEscaped(os, text);
}

} // namespace

/// @brief Entry point for `viper eval [options] [code]`.
/// @param argc Argument count (after "eval" is stripped).
/// @param argv Argument vector.
/// @return 0 on success, 1 on usage error, 2 on compile/eval error, 3 on trap.
int cmdEval(int argc, char **argv) {
    std::string lang = "zia";
    std::string code;
    bool haveCode = false;
    bool jsonOutput = false;
    bool wantType = false;
    bool wantIL = false;

    for (int i = 0; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printEvalUsage(std::cout);
            return 0;
        } else if (arg == "--json") {
            jsonOutput = true;
        } else if (arg == "--type") {
            wantType = true;
        } else if (arg == "--il") {
            wantIL = true;
        } else if (arg == "--lang") {
            if (i + 1 >= argc) {
                std::cerr << "error: --lang requires 'zia' or 'basic'\n";
                return 1;
            }
            lang = argv[++i];
        } else if (arg.substr(0, 7) == "--lang=") {
            lang = std::string(arg.substr(7));
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "error: unknown flag: " << arg << "\n";
            printEvalUsage(std::cerr);
            return 1;
        } else if (!haveCode) {
            code = std::string(arg);
            haveCode = true;
        } else {
            std::cerr << "error: multiple code arguments; quote the snippet as one argument\n";
            return 1;
        }
    }

    if (lang != "zia" && lang != "basic") {
        std::cerr << "error: --lang must be 'zia' or 'basic'\n";
        return 1;
    }
    if (lang != "zia" && (wantType || wantIL)) {
        std::cerr << "error: --type and --il are only supported with --lang zia\n";
        return 1;
    }

    if (!haveCode) {
        code.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    }
    if (code.empty()) {
        std::cerr << "error: no code to evaluate (pass a snippet or pipe it on stdin)\n";
        return 1;
    }

    std::unique_ptr<viper::repl::ReplAdapter> adapter;
    if (lang == "basic")
        adapter = std::make_unique<viper::repl::BasicReplAdapter>();
    else
        adapter = std::make_unique<viper::repl::ZiaReplAdapter>();

    // Type/IL queries run before evaluation so they reflect the same session
    // state the snippet itself sees (an empty session).
    std::string exprType;
    if (wantType)
        exprType = adapter->getExprType(code);
    std::string ilText;
    if (wantIL)
        ilText = adapter->getIL(code);

    auto result = adapter->eval(code);

    if (jsonOutput) {
        std::ostream &os = std::cout;
        os << "{\"success\":" << (result.success ? "true" : "false")
           << ",\"trapped\":" << (result.trapped ? "true" : "false") << ",\"resultType\":";
        printJsonString(os, resultTypeName(result.resultType));
        os << ",\"output\":";
        printJsonString(os, result.output);
        os << ",\"error\":";
        printJsonString(os, result.errorMessage);
        if (wantType) {
            os << ",\"type\":";
            printJsonString(os, exprType);
        }
        if (wantIL) {
            os << ",\"il\":";
            printJsonString(os, ilText);
        }
        os << "}\n";
    } else {
        if (wantType)
            std::cout << "type: " << exprType << "\n";
        if (!result.output.empty()) {
            std::cout << result.output;
            if (result.output.back() != '\n')
                std::cout << '\n';
        }
        if (wantIL)
            std::cout << ilText << (ilText.empty() || ilText.back() == '\n' ? "" : "\n");
        if (!result.success && !result.errorMessage.empty())
            std::cerr << result.errorMessage << "\n";
    }

    if (result.trapped)
        return 3;
    return result.success ? 0 : 2;
}
