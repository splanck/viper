//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/perf/runtime_classes/bench_string_length.cpp
// Purpose: Microbench comparing procedural Len(s) vs property s.Length lowering.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicCompiler.hpp"
#include "support/source_manager.hpp"
#include "vm/VM.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace il::frontends::basic;

namespace
{

struct BenchOut
{
    double ms;
    int64_t exitCode;
};

BenchOut runBasic(const std::string &src)
{
    il::support::SourceManager sm;
    BasicCompilerOptions opts{};
    BasicCompilerInput input{src, "bench.bas"};
    auto result = compileBasic(input, opts, sm);
    if (!result.succeeded())
    {
        std::cerr << "compile failed\n";
        return {0.0, -1};
    }
    il::vm::VM vm(result.module);
    const auto start = std::chrono::steady_clock::now();
    const int64_t code = vm.run();
    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(end - start).count();
    return {ms, code};
}

std::string programLen(size_t iters)
{
    // Sum Len(s) over N iterations
    return "10 DIM s AS STRING\n"
           "20 LET s = \"abcd\"\n"
           "30 LET x = 0\n"
           "40 FOR i = 1 TO " +
           std::to_string(iters) +
           "\n"
           "50 LET x = x + LEN(s)\n"
           "60 NEXT\n"
           "70 PRINT x\n";
}

std::string programProp(size_t iters)
{
    // Sum s.Length over N iterations
    return "10 DIM s AS STRING\n"
           "20 LET s = \"abcd\"\n"
           "30 LET x = 0\n"
           "40 FOR i = 1 TO " +
           std::to_string(iters) +
           "\n"
           "50 LET x = x + s.Length\n"
           "60 NEXT\n"
           "70 PRINT x\n";
}

} // namespace

int main(int argc, char **argv)
{
    size_t iters = 2'000'000; // tune as needed for environment
    if (argc >= 2)
        iters = static_cast<size_t>(std::strtoull(argv[1], nullptr, 10));

    auto a = runBasic(programLen(iters));
    auto b = runBasic(programProp(iters));

    std::cout << "bench_len_ms=" << a.ms << " bench_prop_ms=" << b.ms << " iters=" << iters << "\n";
    return 0;
}
