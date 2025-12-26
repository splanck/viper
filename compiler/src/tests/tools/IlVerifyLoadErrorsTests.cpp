//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/tools/IlVerifyLoadErrorsTests.cpp
// Purpose: Test il-verify CLI error handling for file, parse, and verify errors.
// Key invariants: Exit code is non-zero and stderr contains diagnostics on failure.
// Ownership/Lifetime: Test owns temporary files and stream capture buffers.
// Links: src/tools/il-verify/il-verify.cpp
//
//===----------------------------------------------------------------------===//

#include "support/source_manager.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace il::tools::verify
{
int runCLI(
    int argc, char **argv, std::ostream &out, std::ostream &err, il::support::SourceManager &sm);
} // namespace il::tools::verify

namespace
{
std::filesystem::path repoRoot()
{
    const auto sourcePath = std::filesystem::absolute(std::filesystem::path(__FILE__));
    return sourcePath.parent_path().parent_path().parent_path();
}

int testMissingFileError()
{
    char exeName[] = "il-verify";
    char pathArg[] = "/definitely/not/present.il";
    char *argv[] = {exeName, pathArg};

    il::support::SourceManager sm;
    std::ostringstream outStream;
    std::ostringstream errStream;

    int rc = il::tools::verify::runCLI(2, argv, outStream, errStream, sm);

    // Should fail with non-zero exit code
    assert(rc != 0);
    // stdout should be empty (no "OK")
    assert(outStream.str().empty());
    // stderr should contain the path
    assert(errStream.str().find("/definitely/not/present.il") != std::string::npos);

    return 0;
}

int testParseError()
{
    const auto root = repoRoot();
    std::string pathStr = (root / "tests/il/parse/mismatched_paren.il").string();
    std::vector<char> pathStorage(pathStr.begin(), pathStr.end());
    pathStorage.push_back('\0');

    char exeName[] = "il-verify";
    char *argv[] = {exeName, pathStorage.data()};

    il::support::SourceManager sm;
    std::ostringstream outStream;
    std::ostringstream errStream;

    int rc = il::tools::verify::runCLI(2, argv, outStream, errStream, sm);

    // Should fail with non-zero exit code
    assert(rc != 0);
    // stdout should be empty (no "OK")
    assert(outStream.str().empty());
    // stderr should contain error output
    assert(!errStream.str().empty());

    return 0;
}

int testVerifyError()
{
    const auto root = repoRoot();
    std::string pathStr = (root / "tests/il/negatives/unbalanced_eh.il").string();
    std::vector<char> pathStorage(pathStr.begin(), pathStr.end());
    pathStorage.push_back('\0');

    char exeName[] = "il-verify";
    char *argv[] = {exeName, pathStorage.data()};

    il::support::SourceManager sm;
    std::ostringstream outStream;
    std::ostringstream errStream;

    int rc = il::tools::verify::runCLI(2, argv, outStream, errStream, sm);

    // Should fail with non-zero exit code
    assert(rc != 0);
    // stdout should be empty (no "OK")
    assert(outStream.str().empty());
    // stderr should contain error output
    assert(!errStream.str().empty());

    return 0;
}

int testSuccess()
{
    const auto root = repoRoot();
    std::string pathStr = (root / "tests/data/loop.il").string();
    std::vector<char> pathStorage(pathStr.begin(), pathStr.end());
    pathStorage.push_back('\0');

    char exeName[] = "il-verify";
    char *argv[] = {exeName, pathStorage.data()};

    il::support::SourceManager sm;
    std::ostringstream outStream;
    std::ostringstream errStream;

    int rc = il::tools::verify::runCLI(2, argv, outStream, errStream, sm);

    // Should succeed with zero exit code
    assert(rc == 0);
    // stdout should contain "OK"
    assert(outStream.str().find("OK") != std::string::npos);
    // stderr should be empty
    assert(errStream.str().empty());

    return 0;
}

int testVersionFlag()
{
    char exeName[] = "il-verify";
    char versionArg[] = "--version";
    char *argv[] = {exeName, versionArg};

    il::support::SourceManager sm;
    std::ostringstream outStream;
    std::ostringstream errStream;

    int rc = il::tools::verify::runCLI(2, argv, outStream, errStream, sm);

    // Should succeed with zero exit code
    assert(rc == 0);
    // stdout should contain "IL v"
    assert(outStream.str().find("IL v") != std::string::npos);
    // stderr should be empty
    assert(errStream.str().empty());

    return 0;
}

int testUsageError()
{
    char exeName[] = "il-verify";
    char *argv[] = {exeName};

    il::support::SourceManager sm;
    std::ostringstream outStream;
    std::ostringstream errStream;

    int rc = il::tools::verify::runCLI(1, argv, outStream, errStream, sm);

    // Should fail with non-zero exit code
    assert(rc != 0);
    // stdout should be empty
    assert(outStream.str().empty());
    // stderr should contain usage
    assert(errStream.str().find("Usage:") != std::string::npos);

    return 0;
}
} // namespace

int main()
{
    int result = 0;
    result |= testMissingFileError();
    result |= testParseError();
    result |= testVerifyError();
    result |= testSuccess();
    result |= testVersionFlag();
    result |= testUsageError();
    return result;
}
