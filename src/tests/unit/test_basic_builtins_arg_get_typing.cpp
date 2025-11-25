//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_builtins_arg_get_typing.cpp
// Purpose: Ensure ARG$ requires integer index (registry-driven semantic signature).
// Key invariants: Passing a string to ARG$ should fail semantic analysis.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicCompiler.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    const std::string src = "10 PRINT ARG$(\"x\")\n20 END\n";
    SourceManager sm;
    BasicCompilerOptions opts{};
    BasicCompilerInput input{src, "arg_typing.bas"};
    auto result = compileBasic(input, opts, sm);
    // Expect failure because ARG$ requires an integer index.
    assert(!result.succeeded());
    return 0;
}
