// File: src/support/diagnostics.cpp
// Purpose: Implements diagnostic reporting utilities.
// Key invariants: None.
// Ownership/Lifetime: Diagnostic engine owns stored messages.
// Links: docs/class-catalog.md

#include "diagnostics.hpp"

namespace il::support
{
/// @brief Record a diagnostic and update counters.
/// @param d Diagnostic to store.
/// @note Increments error or warning counts based on severity.
void DiagnosticEngine::report(Diagnostic d)
{
    if (d.severity == Severity::Error)
        ++errors_;
    else if (d.severity == Severity::Warning)
        ++warnings_;
    diags_.push_back(std::move(d));
}

/// @brief Convert a severity enum to a string.
/// @param s Severity value to convert.
/// @return Lowercase string representation of @p s.
static const char *toString(Severity s)
{
    switch (s)
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

/// @brief Print all recorded diagnostics.
/// @param os Output stream receiving diagnostic text.
/// @param sm Optional source manager for resolving locations.
/// @note Each diagnostic appears on its own line with severity and message.
void DiagnosticEngine::printAll(std::ostream &os, const SourceManager *sm) const
{
    for (const auto &d : diags_)
    {
        if (d.loc.isValid() && sm)
        {
            auto path = sm->getPath(d.loc.file_id);
            os << path << ":" << d.loc.line << ":" << d.loc.column << ": ";
        }
        os << toString(d.severity) << ": " << d.message << '\n';
    }
}
} // namespace il::support
