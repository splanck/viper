//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: support/diag_catalog.cpp
// Purpose: Implements diagnostic-code catalog lookup with prefix fallback.
// Key invariants:
//   - The catalog is materialized once and never mutated.
//   - Prefix matching checks the most specific families first.
// Ownership/Lifetime:
//   - Static data with program lifetime.
// Links: support/diag_catalog.hpp, support/diag_catalog.def
//
//===----------------------------------------------------------------------===//

#include "support/diag_catalog.hpp"

#include <algorithm>
#include <cctype>

namespace il::support {

const std::vector<DiagCatalogEntry> &diagCatalog() {
    static const std::vector<DiagCatalogEntry> entries = {
#define DIAG_CODE(code, subsystem, summary) DiagCatalogEntry{code, subsystem, summary},
#include "support/diag_catalog.def"
#undef DIAG_CODE
    };
    return entries;
}

const DiagCatalogEntry *findDiagCode(std::string_view code) {
    const auto &entries = diagCatalog();
    auto it = std::find_if(entries.begin(), entries.end(), [&](const DiagCatalogEntry &e) {
        return e.code == code;
    });
    return it == entries.end() ? nullptr : &*it;
}

std::optional<std::string_view> diagCodeFamily(std::string_view code) {
    struct Family {
        std::string_view prefix;
        std::string_view description;
    };
    // Most specific prefixes first.
    static constexpr Family kFamilies[] = {
        {"V-ZIA-LEX", "Zia lexer diagnostics"},
        {"V-ZIA-PARSE", "Zia parser diagnostics"},
        {"V-ZIA-LOWER", "Zia lowering invariant diagnostics"},
        {"V-ZIA-", "Zia semantic analysis diagnostics"},
        {"V-IL-", "IL verification diagnostics"},
        {"V-BC-", "Bytecode compiler diagnostics"},
        {"V-CG-", "Native codegen diagnostics"},
        {"V-OPT-", "IL optimizer pipeline diagnostics"},
        {"V-SRC-", "Source loading and registration diagnostics"},
        {"V-LSP-", "Language server bridge diagnostics"},
        {"IL", "IL parser/serializer diagnostics"},
    };
    for (const auto &family : kFamilies) {
        if (code.size() >= family.prefix.size() &&
            code.substr(0, family.prefix.size()) == family.prefix)
            return family.description;
    }

    auto allDigitsFrom = [&](size_t start) {
        if (code.size() <= start)
            return false;
        for (size_t i = start; i < code.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(code[i])))
                return false;
        }
        return true;
    };
    if (!code.empty() && code[0] == 'B' && allDigitsFrom(1)) {
        if (code.size() >= 2 && code[1] == '9')
            return "BASIC frontend warnings";
        return "BASIC frontend diagnostics";
    }
    if (!code.empty() && code[0] == 'W' && allDigitsFrom(1))
        return "Zia warnings";
    if (!code.empty() && code[0] == 'E' && allDigitsFrom(1))
        return "BASIC semantic analysis diagnostics";
    return std::nullopt;
}

} // namespace il::support
