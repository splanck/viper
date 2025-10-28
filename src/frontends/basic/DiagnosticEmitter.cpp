//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC front-end diagnostic emitter responsible for capturing,
// formatting, and printing diagnostics with caret annotations and codes.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Diagnostic emitter used by the BASIC front end.
/// @details Wraps @ref il::support::DiagnosticEngine to capture diagnostics,
///          store full source text, and later print caret-highlighted output.

#include "frontends/basic/DiagnosticEmitter.hpp"
#include <algorithm>
#include <sstream>

namespace il::frontends::basic
{

/// @brief Initialize an emitter bound to a diagnostic engine and source manager.
/// @details Stores references to the engine and manager so later emissions can
///          print caret information without additional wiring.
/// @param de Engine used to report diagnostics immediately.
/// @param sm Manager providing file path lookups for caret output.
DiagnosticEmitter::DiagnosticEmitter(il::support::DiagnosticEngine &de,
                                     const il::support::SourceManager &sm)
    : de_(de), sm_(sm)
{
}

/// @brief Register full source text for a file identifier.
/// @details Caches the provided buffer so caret printing can fetch the
///          surrounding line quickly.
/// @param fileId Key used by diagnostics to reference this source.
/// @param source Contents of the file to enable caret printing.
void DiagnosticEmitter::addSource(uint32_t fileId, std::string source)
{
    sources_[fileId] = std::move(source);
}

/// @brief Report a diagnostic and store it for later printing.
/// @details Immediately forwards the diagnostic to the underlying engine and
///          records a copy so caret-formatted output can be produced in order of
///          emission.
/// @param sev Severity level of the diagnostic.
/// @param code Project-defined diagnostic code.
/// @param loc Source location associated with the issue.
/// @param length Highlight length; zero defaults to one caret.
/// @param message Human-readable explanation of the problem.
void DiagnosticEmitter::emit(il::support::Severity sev,
                             std::string code,
                             il::support::SourceLoc loc,
                             uint32_t length,
                             std::string message)
{
    de_.report({sev, message, loc});
    entries_.push_back({sev, std::move(code), std::move(message), loc, length});
}

/// @brief Emit an error when a different token was encountered than expected.
/// @param got Token actually observed.
/// @param expect Token that was required.
/// @param loc Source location of the unexpected token.
/// @details Formats diagnostic B0001, describing the mismatch between expected
///          and observed tokens.
void DiagnosticEmitter::emitExpected(TokenKind got, TokenKind expect, il::support::SourceLoc loc)
{
    std::string msg;
    if (expect == TokenKind::Number && got == TokenKind::Identifier)
    {
        msg = "expected label or number";
    }
    else
    {
        msg = std::string("expected ") + tokenKindToString(expect) + ", got " + tokenKindToString(got);
    }
    emit(il::support::Severity::Error, "B0001", loc, 0, std::move(msg));
}

/// @brief Convert severity enum to human-readable string.
/// @details Used when printing diagnostics so the textual level appears alongside
///          the code and message.
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
/// @details Performs a linear scan through the cached buffer to extract the
///          requested 1-based line, returning an empty string when the file or
///          line number is unknown.
/// @param fileId Source file identifier.
/// @param line 1-based line number to fetch.
/// @return Line contents or empty string if unavailable.
std::string DiagnosticEmitter::getLine(uint32_t fileId, uint32_t line) const
{
    if (line == 0)
        return {};

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

/// @brief Print all collected diagnostics with caret markers.
/// @details Streams each stored entry to @p os, including file/line metadata and
///          a caret underline that spans @ref DiagnosticEntry::length
///          characters.
/// @param os Output stream receiving formatted diagnostics.
void DiagnosticEmitter::printAll(std::ostream &os) const
{
    for (const auto &e : entries_)
    {
        if (e.loc.file_id != 0)
        {
            auto path = sm_.getPath(e.loc.file_id);
            if (!path.empty())
            {
                os << path;
                if (e.loc.line != 0)
                {
                    os << ':' << e.loc.line;
                    if (e.loc.column != 0)
                    {
                        os << ':' << e.loc.column;
                    }
                }
                os << ": ";
            }
        }
        os << toString(e.severity) << '[' << e.code << "]: " << e.message << '\n';
        std::string line = getLine(e.loc.file_id, e.loc.line);
        if (!line.empty())
        {
            os << line << '\n';
            uint32_t caretLen = e.length == 0 ? 1 : e.length;
            uint32_t indent = e.loc.column > 0 ? e.loc.column - 1 : 0;
            os << std::string(indent, ' ') << std::string(caretLen, '^') << '\n';
        }
    }
}

/// @brief Retrieve the number of errors emitted so far.
/// @details Forwards to the underlying diagnostic engine so callers can decide
///          whether the compilation pipeline should continue.
/// @return Count of error-severity diagnostics from the underlying engine.
size_t DiagnosticEmitter::errorCount() const
{
    return de_.errorCount();
}

/// @brief Retrieve the number of warnings emitted so far.
/// @details Mirrors @ref errorCount but for warning-severity diagnostics.
/// @return Count of warning-severity diagnostics from the underlying engine.
size_t DiagnosticEmitter::warningCount() const
{
    return de_.warningCount();
}

} // namespace il::frontends::basic
