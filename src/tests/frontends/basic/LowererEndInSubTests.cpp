//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/basic/LowererEndInSubTests.cpp
// Purpose: Verify that END inside a SUB lowers without return-type verifier
//          errors by lowering to a trap (program termination) instead of ret.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <sstream>
#include <string>

using namespace il::frontends::basic;

static il::core::Module lowerSnippet(const std::string &src)
{
    il::support::SourceManager sm;
    uint32_t fid = sm.addFile("end_in_sub.bas");
    Parser parser(src, fid);
    auto program = parser.parseProgram();
    assert(program);
    Lowerer lowerer;
    return lowerer.lowerProgram(*program);
}

int main()
{
    const std::string src =
        "10 SUB ShowTitle\n"
        "20 PRINT \"q to quit\"\n"
        "30 END\n"
        "40 END SUB\n"
        "50 ShowTitle\n"
        "60 END\n";

    il::core::Module m = lowerSnippet(src);
    auto ve = il::verify::Verifier::verify(m);
    assert(ve && "Lowering END inside SUB should verify (trap-based)");
    return 0;
}

