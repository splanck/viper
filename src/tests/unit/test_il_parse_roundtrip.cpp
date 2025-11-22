//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_roundtrip.cpp
// Purpose: Round-trip parse/serialize coverage for parse-roundtrip IL goldens. 
// Key invariants: Serializer reproduces canonical text for new opcode forms.
// Ownership/Lifetime: Test owns all modules and buffers.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "viper/il/IO.hpp"

#include <array>
#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

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
                                                   PARSE_ROUNDTRIP_DIR "/trap_newline.il",
                                                   SWITCH_GOLDEN};

    for (const char *path : files)
    {
        const bool checkNewline = std::string(path).find("trap_newline.il") != std::string::npos;

        std::ifstream input(path);
        std::stringstream buffer;
        buffer << input.rdbuf();
        buffer.seekg(0);

        il::core::Module initial;
        auto parseFirst = il::api::v2::parse_text_expected(buffer, initial);
        assert(parseFirst);

        std::string serialized = il::io::Serializer::toString(initial);
        if (checkNewline)
        {
            assert(serialized.find("\"\\n\"") != std::string::npos);
        }
        std::istringstream second(serialized);
        il::core::Module roundTripped;
        auto parseSecond = il::api::v2::parse_text_expected(second, roundTripped);
        assert(parseSecond);

        std::string finalText = il::io::Serializer::toString(roundTripped);
        if (checkNewline)
        {
            assert(finalText.find("\"\\n\"") != std::string::npos);
        }
        if (!serialized.empty() && serialized.back() == '\n')
            serialized.pop_back();
        if (!finalText.empty() && finalText.back() == '\n')
            finalText.pop_back();
        assert(serialized == finalText);
    }

    return 0;
}
