// File: tests/unit/test_il_function_parser_errors.cpp
// Purpose: Exercise Expected-returning function parser helpers on failure paths.
// Key invariants: Helpers surface structured diagnostics for malformed headers and bodies.
// Ownership/Lifetime: Tests construct modules and parser states locally.
// Links: docs/il-spec.md

#include "il/io/FunctionParser.hpp"
#include "il/io/ParserState.hpp"
#include "il/core/Module.hpp"

#include <cassert>
#include <sstream>
#include <string>

int main()
{
    using il::io::detail::parseBlockHeader;
    using il::io::detail::parseFunction;
    using il::io::detail::parseFunctionHeader;
    using il::io::detail::ParserState;

    // Malformed function header should report a diagnostic.
    {
        il::core::Module m;
        ParserState st{m};
        st.lineNo = 3;
        auto result = parseFunctionHeader("func @broken() i32 {", st);
        assert(!result);
        const std::string &msg = result.error().message;
        assert(msg.find("malformed function header") != std::string::npos);
    }

    // Block parameter missing a colon should trigger the "bad param" diagnostic.
    {
        il::core::Module m;
        ParserState st{m};
        st.lineNo = 5;
        auto headerOk = parseFunctionHeader("func @ok(i32 %x) -> i32 {", st);
        assert(headerOk);
        st.lineNo = 6;
        auto blockErr = parseBlockHeader("entry(%x i32)", st);
        assert(!blockErr);
        const std::string &msg = blockErr.error().message;
        assert(msg.find("bad param") != std::string::npos);
    }

    // Body without an opening block should surface an instruction-placement error.
    {
        il::core::Module m;
        ParserState st{m};
        st.lineNo = 10;
        std::string header = "func @body() -> i32 {";
        std::istringstream body("  ret 0\n}\n");
        auto parseResult = parseFunction(body, header, st);
        assert(!parseResult);
        const std::string &msg = parseResult.error().message;
        assert(msg.find("instruction outside block") != std::string::npos);
    }

    return 0;
}
