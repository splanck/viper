//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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
#include "il/core/Module.hpp"

#include "il/io/ModuleParser.hpp"
#include "il/io/ParserUtil.hpp"
#include "support/diag_expected.hpp"

#include <sstream>
#include <string>

namespace il::io::detail
{
il::support::Expected<void> parseModuleHeader_E(std::istream &is,
                                                std::string &line,
                                                ParserState &st);
}

namespace il::io
{

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
il::support::Expected<void> Parser::parse(std::istream &is, il::core::Module &m)
{
    detail::ParserState st{m};
    std::string line;
    while (std::getline(is, line))
    {
        ++st.lineNo;
        if (st.lineNo == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
        {
            line.erase(0, 3);
        }
        line = trim(line);
        if (line.empty() || line.rfind("//", 0) == 0 || (!line.empty() && line[0] == '#'))
            continue;
        if (auto result = detail::parseModuleHeader_E(is, line, st); !result)
            return result;
    }
    if (!st.sawVersion)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": missing 'il' version directive";
        return il::support::Expected<void>{il::support::makeError({}, oss.str())};
    }
    return {};
}

} // namespace il::io
