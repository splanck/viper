//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_invalid_calling_conv.cpp
// Purpose: Ensure the IL parser reports an error for unknown calling conventions. 
// Key invariants: Parser produces a diagnostic mentioning the invalid convention and source line.
// Ownership/Lifetime: Test owns input buffers and module instance locally.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "support/diagnostics.hpp"

#include <cassert>
#include <fstream>
#include <sstream>

#ifndef PARSE_ERROR_DIR
#error "PARSE_ERROR_DIR must be defined"
#endif

int main()
{
    const char *path = PARSE_ERROR_DIR "/invalid_calling_conv.il";
    std::ifstream in(path);
    std::stringstream buf;
    buf << in.rdbuf();
    buf.seekg(0);

    il::core::Module module;
    auto result = il::api::v2::parse_text_expected(buf, module);
    assert(!result && "parser should reject unknown calling conventions");

    std::ostringstream diag;
    il::support::printDiag(result.error(), diag);
    const std::string message = diag.str();
    assert(message.find("unknown calling convention 'fastcc'") != std::string::npos);
    assert(message.find("line 2") != std::string::npos);

    return 0;
}
