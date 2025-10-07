// File: tests/unit/test_il_parse_roundtrip.cpp
// Purpose: Round-trip parse/serialize coverage for parse-roundtrip IL goldens.
// Key invariants: Serializer reproduces canonical text for new opcode forms.
// Ownership/Lifetime: Test owns all modules and buffers.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "il/io/Serializer.hpp"

#include <array>
#include <cassert>
#include <fstream>
#include <sstream>

#ifndef PARSE_ROUNDTRIP_DIR
#error "PARSE_ROUNDTRIP_DIR must be defined"
#endif

int main()
{
    constexpr std::array<const char *, 9> files = {PARSE_ROUNDTRIP_DIR "/checked-arith.il",
                                                   PARSE_ROUNDTRIP_DIR "/checked-divrem.il",
                                                   PARSE_ROUNDTRIP_DIR "/cast-checks.il",
                                                   PARSE_ROUNDTRIP_DIR "/errors_eh.il",
                                                   PARSE_ROUNDTRIP_DIR "/idx_chk.il",
                                                   PARSE_ROUNDTRIP_DIR "/err_access.il",
                                                   PARSE_ROUNDTRIP_DIR "/target_directive.il",
                                                   PARSE_ROUNDTRIP_DIR "/globals_literals.il",
                                                   SWITCH_GOLDEN};

    for (const char *path : files)
    {
        std::ifstream input(path);
        std::stringstream buffer;
        buffer << input.rdbuf();
        buffer.seekg(0);

        il::core::Module initial;
        auto parseFirst = il::api::v2::parse_text_expected(buffer, initial);
        assert(parseFirst);

        std::string serialized = il::io::Serializer::toString(initial);
        std::istringstream second(serialized);
        il::core::Module roundTripped;
        auto parseSecond = il::api::v2::parse_text_expected(second, roundTripped);
        assert(parseSecond);

        std::string finalText = il::io::Serializer::toString(roundTripped);
        if (!serialized.empty() && serialized.back() == '\n')
            serialized.pop_back();
        if (!finalText.empty() && finalText.back() == '\n')
            finalText.pop_back();
        assert(serialized == finalText);
    }

    return 0;
}
