// File: tests/unit/test_il_parse_negative.cpp
// Purpose: Ensure IL parser rejects malformed constructs, including block params, branch arguments,
// and numeric literals. Key invariants: Parser returns false for invalid input. Ownership/Lifetime:
// Test owns all modules and buffers locally. Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include <cassert>
#include <fstream>
#include <sstream>

int main()
{
    const char *files[] = {BAD_DIR "/mismatched_paren.il",
                           BAD_DIR "/bad_arg_count.il",
                           BAD_DIR "/unknown_param_type.il",
                           BAD_DIR "/bad_i128.il",
                           BAD_DIR "/bad_int_literal.il",
                           BAD_DIR "/bad_float_literal.il",
                           BAD_DIR "/alloca_missing_size.il",
                           BAD_DIR "/target_missing_quotes.il",
                           BAD_DIR "/block_param_missing_name.il",
                           BAD_DIR "/br_trailing_token.il",
                           BAD_DIR "/switch_trailing_token.il",
                           BAD_DIR "/global_missing_name.il",
                           BAD_DIR "/duplicate_extern.il"};
    for (const char *path : files)
    {
        std::ifstream in(path);
        std::stringstream buf;
        buf << in.rdbuf();
        buf.seekg(0);
        il::core::Module m;
        auto parse = il::api::v2::parse_text_expected(buf, m);
        assert(!parse);
    }
    return 0;
}
