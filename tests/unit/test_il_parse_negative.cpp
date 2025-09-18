// File: tests/unit/test_il_parse_negative.cpp
// Purpose: Ensure IL parser rejects malformed constructs, including block params, branch arguments,
// and numeric literals. Key invariants: Parser returns false for invalid input. Ownership/Lifetime:
// Test owns all modules and buffers locally. Links: docs/il-spec.md

#include "il/io/Parser.hpp"
#include "il/core/Module.hpp"
#include <cassert>
#include <fstream>
#include <sstream>

int main()
{
    const char *files[] = {BAD_DIR "/mismatched_paren.il",
                           BAD_DIR "/bad_arg_count.il",
                           BAD_DIR "/unknown_param_type.il",
                           BAD_DIR "/bad_i32.il",
                           BAD_DIR "/bad_int_literal.il",
                           BAD_DIR "/bad_float_literal.il",
                           BAD_DIR "/alloca_missing_size.il"};
    for (const char *path : files)
    {
        std::ifstream in(path);
        std::stringstream buf;
        buf << in.rdbuf();
        buf.seekg(0);
        il::core::Module m;
        std::ostringstream err;
        bool ok = il::io::Parser::parse(buf, m, err);
        assert(!ok);
    }
    return 0;
}
