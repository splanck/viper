//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/basic/RuntimePropertyDiagTests.cpp
// Purpose: Verify diagnostics for runtime class property assignment (read-only). 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicCompiler.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <sstream>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    const char *src =
        "10 DIM s AS Viper.String\n"
        "20 LET s = \"abcd\"\n"
        "30 s.Length = 5\n"
        "40 END\n";

    SourceManager sm;
    BasicCompilerOptions opts{};
    BasicCompilerInput input{src, "prop_readonly.bas"};
    auto result = compileBasic(input, opts, sm);
    assert(result.emitter);

    std::ostringstream oss;
    result.emitter->printAll(oss);
    auto out = oss.str();
    // Expect a read-only diagnostic
    assert(out.find("error[E_PROP_READONLY]") != std::string::npos);
    return 0;
}

