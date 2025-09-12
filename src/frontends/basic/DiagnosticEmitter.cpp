// File: src/frontends/basic/DiagnosticEmitter.cpp
// Purpose: Implements BASIC diagnostic formatting with codes and carets.
// Key invariants: Diagnostics printed in emission order.
// Ownership/Lifetime: Holds copies of source text; borrows engine and manager.
// Links: docs/class-catalog.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include <algorithm>
#include <sstream>

namespace il::frontends::basic
{

DiagnosticEmitter::DiagnosticEmitter(il::support::DiagnosticEngine &de,
                                     const il::support::SourceManager &sm)
    : de_(de), sm_(sm)
{
}

/// @brief Register source text for a file id.
/// @param fileId Identifier from SourceManager.
/// @param source Full contents of the source file.
/// @effects Stores @p source for later diagnostic printing.
void DiagnosticEmitter::addSource(uint32_t fileId, std::string source)
{
    sources_[fileId] = std::move(source);
}

/// @brief Emit diagnostic with BASIC error code and caret range.
/// @param sev Severity level.
/// @param code BASIC error code (e.g., B1001).
/// @param loc Start location of the diagnostic.
/// @param length Number of characters to underline (0 -> 1 caret).
/// @param message Human-readable explanation.
/// @effects Reports to diagnostic engine and records entry for later printing.
void DiagnosticEmitter::emit(il::support::Severity sev,
                             std::string code,
                             il::support::SourceLoc loc,
                             uint32_t length,
                             std::string message)
{
    de_.report({sev, message, loc});
    entries_.push_back({sev, std::move(code), std::move(message), loc, length});
}

/// @brief Emit standardized "expected vs got" parse diagnostic.
/// @param got Actual token encountered.
/// @param expect Expected token kind.
/// @param loc Source location of the unexpected token.
/// @effects Constructs message and forwards to emit with error code B0001.
void DiagnosticEmitter::emitExpected(TokenKind got, TokenKind expect, il::support::SourceLoc loc)
{
    std::string msg =
        std::string("expected ") + tokenKindToString(expect) + ", got " + tokenKindToString(got);
    emit(il::support::Severity::Error, "B0001", loc, 0, std::move(msg));
}

/// @brief Convert severity enum to human-readable string.
/// @param s Severity to convert.
/// @return Null-terminated severity name.
static const char *toString(il::support::Severity s)
{
    using il::support::Severity;
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

/// @brief Retrieve a specific line from stored source text.
/// @param fileId Source file identifier.
/// @param line 1-based line number to fetch.
/// @return Line contents or empty string if unavailable.
std::string DiagnosticEmitter::getLine(uint32_t fileId, uint32_t line) const
{
    auto it = sources_.find(fileId);
    if (it == sources_.end())
        return {};
    const std::string &src = it->second;
    size_t start = 0;
    for (uint32_t l = 1; l < line; ++l)
    {
        size_t pos = src.find('\n', start);
        if (pos == std::string::npos)
            return {};
        start = pos + 1;
    }
    size_t end = src.find('\n', start);
    if (end == std::string::npos)
        end = src.size();
    return src.substr(start, end - start);
}

void DiagnosticEmitter::printAll(std::ostream &os) const
{
    for (const auto &e : entries_)
    {
        auto path = sm_.getPath(e.loc.file_id);
        os << path << ':' << e.loc.line << ':' << e.loc.column << ": " << toString(e.severity)
           << '[' << e.code << "]: " << e.message << '\n';
        std::string line = getLine(e.loc.file_id, e.loc.line);
        if (!line.empty())
        {
            os << line << '\n';
            uint32_t caretLen = e.length == 0 ? 1 : e.length;
            os << std::string(e.loc.column - 1, ' ') << std::string(caretLen, '^') << '\n';
        }
    }
}

} // namespace il::frontends::basic
