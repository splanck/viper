// File: src/il/io/Parser.cpp
// Purpose: Implements the façade parser entry point for textual IL.
// Key invariants: Delegates work to module/function/instruction helpers.
// Ownership/Lifetime: Operates on external module references without owning them.
// Links: docs/il-spec.md

#include "il/io/Parser.hpp"
#include "il/core/Module.hpp"

#include "il/io/ModuleParser.hpp"
#include "il/io/ParserUtil.hpp"

#include <sstream>
#include <string>

namespace il::io
{

il::support::Expected<void> Parser::parse(std::istream &is, il::core::Module &m)
{
    std::ostringstream err;
    if (Parser::parse(is, m, err))
        return {};
    return std::unexpected(makeError({}, err.str()));
}

bool Parser::parse(std::istream &is, il::core::Module &m, std::ostream &err)
{
    detail::ParserState st{m};
    std::string line;
    while (std::getline(is, line))
    {
        ++st.lineNo;
        line = trim(line);
        if (line.empty() || line.rfind("//", 0) == 0)
            continue;
        if (!detail::parseModuleHeader(is, line, st, err))
            return false;
    }
    return !st.hasError;
}

} // namespace il::io
