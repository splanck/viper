//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_builtins_args_cmd_semantics.cpp
// Purpose: Verify ARGC/ARG$/COMMAND$ semantic arity validation uses registry arity (no table
// Key invariants: To be documented.
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
    // COMMAND$ should accept 0 args only.
    {
        const std::string src = "10 PRINT COMMAND$(1)\n20 END\n";
        SourceManager sm;
        BasicCompilerOptions opts{};
        BasicCompilerInput input{src, "cmd_arity.bas"};
        auto result = compileBasic(input, opts, sm);
        assert(!result.succeeded());
    }

    // ARG$ requires exactly one argument.
    {
        const std::string src = "10 PRINT ARG$()\n20 END\n";
        SourceManager sm;
        BasicCompilerOptions opts{};
        BasicCompilerInput input{src, "arg_arity.bas"};
        auto result = compileBasic(input, opts, sm);
        assert(!result.succeeded());
    }

    return 0;
}
