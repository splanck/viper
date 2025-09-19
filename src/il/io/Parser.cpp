// File: src/il/io/Parser.cpp
// Purpose: Implements the façade parser entry point for textual IL.
// Key invariants: Delegates work to module/function/instruction helpers.
// Ownership/Lifetime: Operates on external module references without owning them.
// Links: docs/il-spec.md

#include "il/io/Parser.hpp"
#include "il/core/Module.hpp"

#include "il/io/ModuleParser.hpp"
#include "il/io/ParserUtil.hpp"
#include "support/diag_expected.hpp"

#include <string>

namespace il::io::detail
{
il::support::Expected<void> parseModuleHeader_E(std::istream &is, std::string &line,
                                                ParserState &st);
}

namespace il::io
{

il::support::Expected<void> Parser::parse(std::istream &is, il::core::Module &m)
{
    detail::ParserState st{m};
    std::string line;
    while (std::getline(is, line))
    {
        ++st.lineNo;
        line = trim(line);
        if (line.empty() || line.rfind("//", 0) == 0)
            continue;
        if (auto result = detail::parseModuleHeader_E(is, line, st); !result)
            return result;
    }
    return {};
}

} // namespace il::io
