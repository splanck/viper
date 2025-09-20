// File: src/support/diag_expected.cpp
// Purpose: Implements diagnostic helper functions shared across Expected utilities.
// License: MIT License. See LICENSE in the project root for details.
// Key invariants: Diagnostics consistently report severity and optional source locations.
// Ownership/Lifetime: Diagnostics own their message strings; no additional ownership here.
// Links: docs/class-catalog.md

#include "diag_expected.hpp"

namespace il::support
{
namespace detail
{
const char *diagSeverityToString(Severity severity)
{
    switch (severity)
    {
        case Severity::Note:
            return "note";
        case Severity::Warning:
            return "warning";
        case Severity::Error:
            return "error";
    }
    return "";
}
} // namespace detail

Diag makeError(SourceLoc loc, std::string msg)
{
    return Diag{Severity::Error, std::move(msg), loc};
}

void printDiag(const Diag &diag, std::ostream &os, const SourceManager *sm)
{
    if (diag.loc.isValid() && sm)
    {
        auto path = sm->getPath(diag.loc.file_id);
        os << path << ":" << diag.loc.line << ":" << diag.loc.column << ": ";
    }
    os << detail::diagSeverityToString(diag.severity) << ": " << diag.message
       << '\n';
}
} // namespace il::support
