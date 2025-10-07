// File: tests/unit/test_il_function_parser_errors.cpp
// Purpose: Exercise Expected-returning function parser helpers on failure paths.
// Key invariants: Helpers surface structured diagnostics for malformed headers and bodies.
// Ownership/Lifetime: Tests construct modules and parser states locally.
// Links: docs/il-guide.md#reference

#include "il/io/FunctionParser.hpp"
#include "il/io/ParserState.hpp"
#include "il/core/Module.hpp"
#include "support/source_location.hpp"

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
        auto result = parseFunctionHeader("func @broken() i64 {", st);
        assert(!result);
        const std::string &msg = result.error().message;
        assert(msg.find("malformed function header") != std::string::npos);
    }

    // Unknown parameter type should surface an error and avoid mutating the module.
    {
        il::core::Module m;
        ParserState st{m};
        st.lineNo = 4;
        auto result = parseFunctionHeader("func @oops(bad %x) -> i64 {", st);
        assert(!result);
        const std::string &msg = result.error().message;
        assert(msg.find("unknown param type") != std::string::npos);
        assert(m.functions.empty());
    }

    // Block parameter missing a colon should trigger the "bad param" diagnostic.
    {
        il::core::Module m;
        ParserState st{m};
        st.lineNo = 5;
        auto headerOk = parseFunctionHeader("func @ok(i64 %x) -> i64 {", st);
        assert(headerOk);
        st.lineNo = 6;
        auto blockErr = parseBlockHeader("entry(%x i64)", st);
        assert(!blockErr);
        const std::string &msg = blockErr.error().message;
        assert(msg.find("bad param") != std::string::npos);
    }

    // Body without an opening block should surface an instruction-placement error.
    {
        il::core::Module m;
        ParserState st{m};
        st.lineNo = 10;
        std::string header = "func @body() -> i64 {";
        std::istringstream body("  ret 0\n}\n");
        auto parseResult = parseFunction(body, header, st);
        assert(!parseResult);
        const std::string &msg = parseResult.error().message;
        assert(msg.find("instruction outside block") != std::string::npos);
    }

    // Subsequent functions after a `.loc` should not inherit the previous location.
    {
        il::core::Module m;
        ParserState st{m};
        std::istringstream module(R"(func @with_loc() -> i32 {
entry:
  .loc 1 10 2
  ret 0
}
func @bad() -> i32 {
entry:
  bogus
}
)");

        std::string header;
        std::getline(module, header);
        st.lineNo = 1;
        auto firstResult = parseFunction(module, header, st);
        assert(firstResult);

        std::getline(module, header);
        st.lineNo = 6;
        auto secondResult = parseFunction(module, header, st);
        assert(!secondResult);
        assert(!secondResult.error().loc.isValid());
    }

    return 0;
}
