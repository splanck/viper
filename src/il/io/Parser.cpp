// File: src/il/io/Parser.cpp
// Purpose: Implements the façade parser entry point for textual IL.
// Key invariants: Delegates work to module/function/instruction helpers.
// Ownership/Lifetime: Operates on external module references without owning them.
// License: MIT (see LICENSE).
// Links: docs/il-guide.md#reference

#include "il/io/Parser.hpp"
#include "il/core/Module.hpp"

#include "il/io/ModuleParser.hpp"
#include "il/io/ParserUtil.hpp"
#include "support/diag_expected.hpp"

#include <sstream>
#include <string>

namespace il::io::detail
{
il::support::Expected<void> parseModuleHeader_E(std::istream &is, std::string &line,
                                                ParserState &st);
}

namespace il::io
{

/**
 * @brief Parse a textual IL module from a stream.
 * @details Constructs a ParserState for the target module and iterates over
 * each input line until EOF. Every iteration bumps the tracked line number,
 * trims leading/trailing whitespace, skips empty lines or lines beginning with
 * "//", and otherwise delegates to detail::parseModuleHeader_E. Any error
 * produced by parseModuleHeader_E is surfaced directly to the caller without
 * further handling.
 */
il::support::Expected<void> Parser::parse(std::istream &is, il::core::Module &m)
{
    detail::ParserState st{m};
    std::string line;
    while (std::getline(is, line))
    {
        ++st.lineNo;
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
