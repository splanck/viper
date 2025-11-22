//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_loc_errors.cpp
// Purpose: Ensure the IL function parser reports malformed .loc directives. 
// Key invariants: ParserState diagnostics identify incorrect location triplets.
// Ownership/Lifetime: Constructs parser state locally for each scenario.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/core/Module.hpp"
#include "il/internal/io/FunctionParser.hpp"
#include "il/internal/io/ParserState.hpp"

#include <cassert>
#include <sstream>
#include <string>

int main()
{
    using il::io::detail::parseFunction;
    using il::io::detail::ParserState;

    il::core::Module m;
    ParserState st{m};
    st.lineNo = 1;

    std::string header = "func @loc() -> i64 {";
    std::istringstream body(R"(entry:
  .loc 1 2
  ret 0
}
)");

    auto result = parseFunction(body, header, st);
    assert(!result);
    const std::string &msg = result.error().message;
    assert(msg.find("malformed .loc directive") != std::string::npos);

    return 0;
}
