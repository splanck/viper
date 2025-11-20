//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Diag.cpp
// Purpose: Implement centralized diagnostics helpers for BASIC frontend.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Diag.hpp"
#include "frontends/basic/IdentifierUtil.hpp"

namespace il::frontends::basic::diagx
{

void ErrorDuplicateProc(DiagnosticEmitter &emitter,
                        std::string_view qname,
                        il::support::SourceLoc first,
                        il::support::SourceLoc second)
{
    // Compose single actionable message including both definition locations.
    std::string whereFirst = emitter.formatFileLine(first);
    std::string whereSecond = emitter.formatFileLine(second);
    std::string msg = std::string("duplicate procedure '") + std::string(qname) +
                      "' first defined at " + (whereFirst.empty() ? "?" : whereFirst) +
                      ", again at " + (whereSecond.empty() ? "?" : whereSecond);
    emitter.emit(il::support::Severity::Error,
                 "B1004",
                 second,
                 static_cast<uint32_t>(qname.size()),
                 std::move(msg));
}

void ErrorUnknownProc(DiagnosticEmitter &emitter,
                      il::support::SourceLoc loc,
                      std::string_view ident,
                      const std::vector<std::string> &tried)
{
    // Canonicalize head identifier if possible to maintain consistency.
    std::string head = CanonicalizeIdent(ident);
    if (head.empty())
        head.assign(ident.begin(), ident.end());

    std::string msg = std::string("unknown procedure '") + head + "'";
    if (!tried.empty())
    {
        msg += " (tried: ";
        for (std::size_t i = 0; i < tried.size(); ++i)
        {
            if (i)
                msg += ", ";
            msg += tried[i];
        }
        msg += ')';
    }
    emitter.emit(il::support::Severity::Error,
                 "B1006",
                 loc,
                 static_cast<uint32_t>(head.size() > 0 ? head.size() : ident.size()),
                 std::move(msg));
}

void ErrorUnknownProcQualified(DiagnosticEmitter &emitter,
                               il::support::SourceLoc loc,
                               std::string_view qname)
{
    emitter.emit(il::support::Severity::Error,
                 "B1006",
                 loc,
                 static_cast<uint32_t>(qname.size()),
                 std::string("unknown procedure '") + std::string(qname) + "'");
}

static std::string formatTriedList(const std::vector<std::string> &tried, std::size_t limit = 8)
{
    if (tried.empty())
        return {};
    std::string s = " (tried: ";
    std::size_t n = std::min(tried.size(), limit);
    for (std::size_t i = 0; i < n; ++i)
    {
        if (i)
            s += ", ";
        s += tried[i];
    }
    if (tried.size() > limit)
    {
        s += ", +" + std::to_string(tried.size() - limit) + " more";
    }
    s += ")";
    return s;
}

void ErrorUnknownProcWithTries(DiagnosticEmitter &emitter,
                               il::support::SourceLoc loc,
                               std::string_view ident,
                               const std::vector<std::string> &tried)
{
    std::string head = CanonicalizeIdent(ident);
    if (head.empty())
        head.assign(ident.begin(), ident.end());
    std::string msg = std::string("unknown procedure '") + head + "'" + formatTriedList(tried);
    emitter.emit(il::support::Severity::Error,
                 "B1006",
                 loc,
                 static_cast<uint32_t>(head.size() > 0 ? head.size() : ident.size()),
                 std::move(msg));
}

void ErrorAmbiguousProc(DiagnosticEmitter &emitter,
                        il::support::SourceLoc loc,
                        std::string_view ident,
                        std::vector<std::string> matches)
{
    std::sort(matches.begin(), matches.end());
    std::string msg = std::string("ambiguous procedure '") + std::string(ident) + "' — matches: ";
    for (std::size_t i = 0; i < matches.size(); ++i)
    {
        if (i)
            msg += ", ";
        msg += matches[i];
    }
    emitter.emit(il::support::Severity::Error,
                 "B2009",
                 loc,
                 static_cast<uint32_t>(ident.size()),
                 std::move(msg));
}

void ErrorUnknownTypeWithTries(DiagnosticEmitter &emitter,
                               il::support::SourceLoc loc,
                               std::string_view ident,
                               const std::vector<std::string> &tried)
{
    std::string head(ident.begin(), ident.end());
    std::string msg = std::string("unknown type '") + head + "'" + formatTriedList(tried);
    emitter.emit(il::support::Severity::Error,
                 "B2111",
                 loc,
                 static_cast<uint32_t>(head.size()),
                 std::move(msg));
}

void NoteAliasExpansion(DiagnosticEmitter &emitter,
                        std::string_view alias,
                        std::string_view targetQn)
{
    std::string msg = std::string("alias '") + std::string(alias) + "' -> " + std::string(targetQn);
    emitter.emit(il::support::Severity::Note, "N0001", {}, 0, std::move(msg));
}

void ErrorBuiltinShadow(DiagnosticEmitter &emitter,
                        std::string_view qname,
                        il::support::SourceLoc loc)
{
    // Emit a distinct diagnostic when a user-defined procedure attempts to shadow
    // a builtin extern (seeded from Viper.* runtime registry). Keep the message
    // concise and actionable.
    std::string msg = std::string("user procedure shadows builtin extern '") +
                      std::string(qname) + "'";
    emitter.emit(il::support::Severity::Error,
                 "E_VIPER_BUILTIN_SHADOW",
                 loc,
                 static_cast<uint32_t>(qname.size()),
                 std::move(msg));
}

} // namespace il::frontends::basic::diagx
