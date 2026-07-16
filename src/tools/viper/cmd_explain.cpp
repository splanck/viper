//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/viper/cmd_explain.cpp
// Purpose: Entry point for `viper explain` and the `--print-error-codes`
//          driver flag. Resolves diagnostic codes against the central catalog
//          and prints human- or machine-readable descriptions.
// Key invariants:
//   - Lookup degrades gracefully: uncataloged codes with a recognized prefix
//     still report their subsystem family.
//   - JSON output is a stable array of {code, subsystem, summary} objects.
// Ownership/Lifetime:
//   - Catalog data is static; no allocation beyond output formatting.
// Links: support/diag_catalog.hpp, docs/tools/debugging.md, docs/tools/cli.md
//
//===----------------------------------------------------------------------===//

#include "support/diag_catalog.hpp"
#include "support/diag_expected.hpp"

#include <iostream>
#include <ostream>
#include <string>
#include <string_view>

namespace {

/// @brief Emit a string as a JSON-escaped, double-quoted literal.
void printJsonString(std::ostream &os, std::string_view text) {
    il::support::printJsonStringEscaped(os, text);
}

/// @brief Print every cataloged code, optionally as JSON.
int printAllCodes(bool json) {
    const auto &entries = il::support::diagCatalog();
    if (json) {
        std::cout << "[";
        bool first = true;
        for (const auto &e : entries) {
            if (!first)
                std::cout << ",";
            first = false;
            std::cout << "{\"code\":";
            printJsonString(std::cout, e.code);
            std::cout << ",\"subsystem\":";
            printJsonString(std::cout, e.subsystem);
            std::cout << ",\"summary\":";
            printJsonString(std::cout, e.summary);
            std::cout << "}";
        }
        std::cout << "]\n";
        return 0;
    }
    for (const auto &e : entries)
        std::cout << e.code << "\t" << e.subsystem << "\t" << e.summary << "\n";
    return 0;
}

void printExplainUsage(std::ostream &os) {
    os << "Usage: viper explain <code> [--json]\n"
       << "       viper explain --list [--json]\n"
       << "\n"
       << "Describe a diagnostic code (e.g., V-ZIA-UNDEFINED, B2001, W008).\n"
       << "--list prints the full catalog; with --json the output is a JSON\n"
       << "array of {code, subsystem, summary} objects on stdout.\n";
}

} // namespace

/// @brief Entry point for `viper explain <code> [--json]` / `viper explain --list`.
/// @param argc Argument count (after "explain" is stripped).
/// @param argv Argument vector.
/// @return 0 when the code (or family) resolves, 1 on usage error or unknown code.
int cmdExplain(int argc, char **argv) {
    bool json = false;
    bool list = false;
    std::string code;

    for (int i = 0; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printExplainUsage(std::cout);
            return 0;
        } else if (arg == "--json") {
            json = true;
        } else if (arg == "--list") {
            list = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "error: unknown flag: " << arg << "\n";
            printExplainUsage(std::cerr);
            return 1;
        } else if (code.empty()) {
            code = std::string(arg);
        } else {
            std::cerr << "error: explain takes a single diagnostic code\n";
            return 1;
        }
    }

    if (list)
        return printAllCodes(json);
    if (code.empty()) {
        printExplainUsage(std::cerr);
        return 1;
    }

    const auto *entry = il::support::findDiagCode(code);
    if (entry) {
        if (json) {
            std::cout << "{\"code\":";
            printJsonString(std::cout, entry->code);
            std::cout << ",\"subsystem\":";
            printJsonString(std::cout, entry->subsystem);
            std::cout << ",\"summary\":";
            printJsonString(std::cout, entry->summary);
            std::cout << "}\n";
        } else {
            std::cout << entry->code << " (" << entry->subsystem << ")\n  " << entry->summary
                      << "\n";
        }
        return 0;
    }

    if (auto family = il::support::diagCodeFamily(code)) {
        if (json) {
            std::cout << "{\"code\":";
            printJsonString(std::cout, code);
            std::cout << ",\"subsystem\":";
            printJsonString(std::cout, *family);
            std::cout << ",\"summary\":\"\"}\n";
        } else {
            std::cout << code << "\n  Not in the catalog yet; the prefix belongs to: " << *family
                      << "\n";
        }
        return 0;
    }

    std::cerr << "error: unknown diagnostic code: " << code << "\n"
              << "Use 'viper explain --list' to see the catalog.\n";
    return 1;
}

/// @brief Implement the `--print-error-codes [--json]` driver flag.
/// @param json True to emit the catalog as JSON.
/// @return 0 on success.
int printErrorCodes(bool json) {
    return printAllCodes(json);
}
