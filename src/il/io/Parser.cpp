//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the façade entry point that wires together the IL text parser.
// The heavy lifting lives in dedicated module/function/instruction helpers;
// this translation unit coordinates them while keeping the public header light.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Top-level textual IL parser implementation.
/// @details Provides the @ref il::io::Parser::parse method used by command-line
///          tools and embedders.  The function streams line-by-line, delegates to
///          specialised helpers, and validates required directives like the
///          module version banner.

#include "il/io/Parser.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"

#include "il/internal/io/ModuleParser.hpp"
#include "il/internal/io/ParserUtil.hpp"
#include "support/diag_expected.hpp"

#include <sstream>
#include <string>

namespace il::io::detail {
il::support::Expected<void> parseModuleHeader_E(std::istream &is,
                                                std::string &line,
                                                ParserState &st);
}

namespace il::io {
namespace {

bool readBoundedLine(std::istream &is,
                     std::string &line,
                     std::size_t maxBytes,
                     bool &tooLong) {
    line.clear();
    tooLong = false;
    char ch = '\0';
    while (is.get(ch)) {
        if (ch == '\n')
            return true;
        if (line.size() >= maxBytes) {
            tooLong = true;
            while (is.get(ch) && ch != '\n') {
            }
            return true;
        }
        line.push_back(ch);
    }
    return !line.empty();
}

il::support::Expected<void> resourceLimitError(unsigned lineNo, std::string_view resource) {
    return il::support::Expected<void>{
        il::support::makeError({}, formatLineDiag(lineNo, std::string("resource limit exceeded: ") +
                                                             std::string(resource)))};
}

} // namespace

/// @brief Parse a textual IL module from a stream.
/// @details Creates a @ref ParserState bound to the destination module, then
///          pulls the source stream line-by-line.  Each iteration increments the
///          current line counter, strips comments and preprocessor directives,
///          and defers to @ref detail::parseModuleHeader_E for directive and
///          instruction handling.  Any diagnostic returned by the helper is
///          propagated verbatim, ensuring callers observe the first failure.
/// @param is Stream providing textual IL.
/// @param m Module populated with parsed definitions.
/// @return Empty Expected when parsing succeeds or the diagnostic describing the
///         first encountered error.
il::support::Expected<void> Parser::parse(std::istream &is,
                                         il::core::Module &m,
                                         const ParserLimits &limits) {
    detail::ParserState st{m, limits};
    std::string line;
    bool lineTooLong = false;
    while (readBoundedLine(is, line, limits.maxLineBytes, lineTooLong)) {
        if (static_cast<std::size_t>(st.lineNo) >= limits.maxLines)
            return resourceLimitError(st.lineNo, "physical lines");
        ++st.lineNo;
        if (lineTooLong)
            return resourceLimitError(st.lineNo, "line bytes");
        if (st.lineNo == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            line.erase(0, 3);
        }
        line = trim(stripInlineComment(line));
        if (line.empty())
            continue;

        if (auto result = detail::parseModuleHeader_E(is, line, st); !result)
            return result;

        if (m.functions.size() > limits.maxFunctions)
            return resourceLimitError(st.lineNo, "functions");
        if (m.externs.size() > limits.maxExterns)
            return resourceLimitError(st.lineNo, "extern declarations");
        if (m.globals.size() > limits.maxGlobals)
            return resourceLimitError(st.lineNo, "global declarations");

        if (st.totalBlocks > limits.maxBlocks)
            return resourceLimitError(st.lineNo, "basic blocks");
        if (st.totalInstructions > limits.maxInstructions)
            return resourceLimitError(st.lineNo, "instructions");
    }
    if (!st.sawVersion) {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": missing 'il' version directive";
        return il::support::Expected<void>{il::support::makeError({}, oss.str())};
    }
    return {};
}

} // namespace il::io
