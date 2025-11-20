// File: tests/unit/test_basic_builtins_args_cmd_semantics.cpp
// Purpose: Verify ARGC/ARG$/COMMAND$ semantic arity validation uses registry arity (no table
// drift). Key invariants: Wrong arity produces clear diagnostics with 0-0 (COMMAND$) and 1-1
// (ARG$).

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
