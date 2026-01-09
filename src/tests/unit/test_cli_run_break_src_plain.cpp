//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_cli_run_break_src_plain.cpp
// Purpose: Ensure cmdRunIL treats bare path:line breakpoints as source breaks.
// Key invariants: `--break foo:7` hits a source breakpoint with the foo module.
// Ownership/Lifetime: Creates a temporary IL file under the OS temp directory.
// Links: src/tools/ilc/cmd_run_il.cpp
//
//===----------------------------------------------------------------------===//

#include "tools/ilc/cli.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{

int runWithArgs(const std::string &file,
                const std::string &flag,
                const std::string &spec,
                std::string &stderrText)
{
    std::vector<std::string> argStorage = {file, flag, spec};
    std::vector<char *> argv;
    argv.reserve(argStorage.size());
    for (auto &arg : argStorage)
    {
        argv.push_back(arg.data());
    }

    std::ostringstream errStream;
    auto *oldBuf = std::cerr.rdbuf(errStream.rdbuf());
    int rc = cmdRunIL(static_cast<int>(argv.size()), argv.data());
    std::cerr.flush();
    std::cerr.rdbuf(oldBuf);

    stderrText = errStream.str();
    return rc;
}

} // namespace

static bool gUsageCalled = false;

void usage()
{
    gUsageCalled = true;
}

int main()
{
    const std::filesystem::path tmpDir =
        std::filesystem::temp_directory_path() / "viper_cli_break_plain";
    std::error_code ec;
    std::filesystem::create_directories(tmpDir, ec);

    const std::filesystem::path ilPath = tmpDir / "foo";
    {
        std::ofstream out(ilPath);
        assert(out.good());
        out << "il 0.2.0\n";
        out << "func @main() -> i64 {\n";
        out << "entry:\n";
        out << "  .loc 1 7 1\n";
        out << "  ret 0\n";
        out << "}\n";
    }

    std::string err;
    gUsageCalled = false;
    int rc = runWithArgs(ilPath.string(), "--break", "foo:7", err);

    assert(rc == 10);
    assert(!gUsageCalled);
    const auto breakPos = err.find("[BREAK] src=foo");
    assert(breakPos != std::string::npos);
    assert(err.find(":7", breakPos) != std::string::npos);

    const uint32_t hugeLine = static_cast<uint32_t>(std::numeric_limits<int>::max()) + 42U;
    {
        std::ofstream out(ilPath);
        assert(out.good());
        out << "il 0.2.0\n";
        out << "func @main() -> i64 {\n";
        out << "entry:\n";
        out << "  .loc 1 " << hugeLine << " 1\n";
        out << "  ret 0\n";
        out << "}\n";
    }

    err.clear();
    gUsageCalled = false;
    const std::string hugeSpec = std::string("foo:") + std::to_string(hugeLine);
    rc = runWithArgs(ilPath.string(), "--break", hugeSpec, err);

    assert(rc == 10);
    assert(!gUsageCalled);
    const auto hugeBreakPos = err.find("[BREAK] src=foo");
    assert(hugeBreakPos != std::string::npos);
    assert(err.find(std::string(":") + std::to_string(hugeLine), hugeBreakPos) !=
           std::string::npos);

    err.clear();
    gUsageCalled = false;
    rc = runWithArgs(ilPath.string(), "--break", "entry", err);

    assert(rc == 10);
    assert(!gUsageCalled);
    const auto labelBreakPos = err.find("[BREAK] fn=@main blk=entry reason=label");
    assert(labelBreakPos != std::string::npos);

    err.clear();
    gUsageCalled = false;
    rc = runWithArgs(ilPath.string(), "--break", "entry:", err);

    assert(rc == 10);
    assert(!gUsageCalled);
    const auto labelColonBreakPos = err.find("[BREAK] fn=@main blk=entry reason=label");
    assert(labelColonBreakPos != std::string::npos);

    std::filesystem::remove(ilPath, ec);
    return 0;
}
