//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/tools/ModuleLoaderTests.cpp
// Purpose: Exercise the shared module loading helpers used by CLI tools.
// Key invariants: Helpers emit diagnostics on failure and succeed for valid inputs.
// Ownership/Lifetime: Test owns constructed modules and diagnostic streams.
// Links: docs/testing.md
//
//===----------------------------------------------------------------------===//

#include "support/source_location.hpp"
#include "support/source_manager.hpp"
#include "tools/common/module_loader.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace
{
std::filesystem::path repoRoot()
{
#ifdef VIPER_REPO_ROOT
    return std::filesystem::path(VIPER_REPO_ROOT) / "src";
#else
    // Fallback: compute from __FILE__ (may not work with all compilers)
    const auto sourcePath = std::filesystem::absolute(std::filesystem::path(__FILE__));
    return sourcePath.parent_path().parent_path().parent_path();
#endif
}
} // namespace

int main()
{
    std::cerr << "Starting test...\n";
    const auto root = repoRoot();
    std::cerr << "Root: " << root << "\n";
    const auto loopPath = (root / "tests/data/loop.il").string();
    std::cerr << "Loop path: " << loopPath << "\n";

    il::core::Module module{};
    std::ostringstream okErrors;
    auto okResult = il::tools::common::loadModuleFromFile(loopPath, module, okErrors);
    if (!okResult.succeeded())
    {
        std::cerr << "Failed to load: " << loopPath << "\n";
        std::cerr << "Root: " << root << "\n";
        std::cerr << "Error: " << okErrors.str() << "\n";
        std::cerr << "Status: " << okResult.statusName() << "\n";
        return 1;
    }
    if (!okErrors.str().empty())
    {
        std::cerr << "Unexpected errors during load: " << okErrors.str() << "\n";
        return 1;
    }
    std::cerr << "Load succeeded, verifying...\n";
    std::ostringstream verifyOk;
    if (!il::tools::common::verifyModule(module, verifyOk))
    {
        std::cerr << "Verification failed: " << verifyOk.str() << "\n";
        return 1;
    }
    if (!verifyOk.str().empty())
    {
        std::cerr << "Unexpected verification output: " << verifyOk.str() << "\n";
        return 1;
    }
    std::cerr << "First module verified OK\n";

    std::cerr << "Testing missing file...\n";
    il::core::Module missingModule{};
    std::ostringstream missingErrors;
    auto missingResult = il::tools::common::loadModuleFromFile(
        "/definitely/not/present.il", missingModule, missingErrors, "cannot open ");
    if (missingResult.status != il::tools::common::LoadStatus::FileError)
    {
        std::cerr << "Expected FileError, got: " << missingResult.statusName() << "\n";
        return 1;
    }
    if (missingErrors.str() != "cannot open /definitely/not/present.il\n")
    {
        std::cerr << "Expected 'cannot open /definitely/not/present.il\\n', got: '" << missingErrors.str() << "'\n";
        return 1;
    }
    std::cerr << "Missing file test passed\n";

    std::cerr << "Testing parse error file...\n";
    il::core::Module parseModule{};
    std::ostringstream parseErrors;
    const auto parseErrorPath = (root / "tests/il/parse/mismatched_paren.il").string();
    std::cerr << "Parse error path: " << parseErrorPath << "\n";
    auto parseResult = il::tools::common::loadModuleFromFile(
        parseErrorPath, parseModule, parseErrors);
    if (parseResult.status != il::tools::common::LoadStatus::ParseError)
    {
        std::cerr << "Expected ParseError, got: " << parseResult.statusName() << "\n";
        std::cerr << "Errors: " << parseErrors.str() << "\n";
        return 1;
    }
    if (parseErrors.str().empty())
    {
        std::cerr << "Expected non-empty parse errors\n";
        return 1;
    }
    std::cerr << "Parse error test passed\n";

    std::cerr << "Testing verify error file...\n";
    il::core::Module verifyModule{};
    std::ostringstream verifyErrors;
    const auto negativePath = (root / "tests/il/negatives/unbalanced_eh.il").string();
    std::cerr << "Negative path: " << negativePath << "\n";
    il::support::SourceManager sm;
    const auto fileId = sm.addFile(negativePath);
    auto negativeLoad =
        il::tools::common::loadModuleFromFile(negativePath, verifyModule, verifyErrors);
    if (!negativeLoad.succeeded())
    {
        std::cerr << "Failed to load negative file: " << negativeLoad.statusName() << "\n";
        std::cerr << "Errors: " << verifyErrors.str() << "\n";
        return 1;
    }
    if (!verifyErrors.str().empty())
    {
        std::cerr << "Unexpected errors loading negative file: " << verifyErrors.str() << "\n";
        return 1;
    }
    std::cerr << "Negative file loaded, checking structure...\n";
    if (!verifyModule.functions.empty() && !verifyModule.functions.front().blocks.empty())
    {
        auto &entry = verifyModule.functions.front().blocks.front();
        if (!entry.instructions.empty())
        {
            entry.instructions.front().loc = {fileId, 4, 3};
            if (entry.instructions.size() > 1)
            {
                entry.instructions[1].loc = {fileId, 5, 3};
            }
        }
    }
    std::cerr << "Verifying negative module (expect failure)...\n";
    std::ostringstream verifyFail;
    if (il::tools::common::verifyModule(verifyModule, verifyFail, &sm))
    {
        std::cerr << "Expected verification to fail, but it succeeded\n";
        return 1;
    }
    const std::string diag = verifyFail.str();
    if (diag.empty())
    {
        std::cerr << "Expected non-empty diagnostic from verification failure\n";
        return 1;
    }
    std::cerr << "Got diagnostic: " << diag << "\n";
    // Normalize paths for comparison (Windows: lowercase, forward slashes)
    auto normalizePath = [](std::string path) {
        std::transform(path.begin(), path.end(), path.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::replace(path.begin(), path.end(), '\\', '/');
        return path;
    };
    const std::string normalizedNegativePath = normalizePath(negativePath);
    const std::string normalizedDiag = normalizePath(diag);
    const std::string prefix = normalizedNegativePath + ":";
    std::cerr << "Expected prefix (normalized): " << prefix << "\n";
    std::cerr << "Diagnostic (normalized): " << normalizedDiag << "\n";
    if (normalizedDiag.rfind(prefix, 0) != 0)
    {
        std::cerr << "Diagnostic does not start with expected path prefix\n";
        return 1;
    }
    std::cerr << "Prefix check passed\n";
    // Use normalized strings for the rest of the parsing
    const auto lineStart = prefix.size();
    const auto lineEnd = normalizedDiag.find(':', lineStart);
    if (lineEnd == std::string::npos)
    {
        std::cerr << "Could not find line number separator\n";
        return 1;
    }
    const auto lineStr = normalizedDiag.substr(lineStart, lineEnd - lineStart);
    auto isDigit = [](unsigned char ch) { return std::isdigit(ch) != 0; };
    if (lineStr.empty() || !std::all_of(lineStr.begin(), lineStr.end(), isDigit))
    {
        std::cerr << "Line number is not valid: '" << lineStr << "'\n";
        return 1;
    }
    const auto columnStart = lineEnd + 1;
    const auto columnEnd = normalizedDiag.find(':', columnStart);
    if (columnEnd == std::string::npos)
    {
        std::cerr << "Could not find column number separator\n";
        return 1;
    }
    const auto columnStr = normalizedDiag.substr(columnStart, columnEnd - columnStart);
    if (columnStr.empty() || !std::all_of(columnStr.begin(), columnStr.end(), isDigit))
    {
        std::cerr << "Column number is not valid: '" << columnStr << "'\n";
        return 1;
    }
    std::cerr << "Line/column parsing passed\n";

    std::cerr << "Test verifyModuleResult - success case\n";
    // Test verifyModuleResult - returns LoadResult instead of bool
    {
        il::core::Module goodModule{};
        std::ostringstream discardErr;
        auto goodLoad = il::tools::common::loadModuleFromFile(
            (root / "tests/data/loop.il").string(), goodModule, discardErr);
        if (!goodLoad.succeeded())
        {
            std::cerr << "  Failed to load good module\n";
            return 1;
        }
        auto verifyResult = il::tools::common::verifyModuleResult(goodModule);
        if (!verifyResult.succeeded())
        {
            std::cerr << "  verifyModuleResult failed unexpectedly\n";
            return 1;
        }
        if (verifyResult.diag.has_value())
        {
            std::cerr << "  verifyModuleResult has unexpected diag\n";
            return 1;
        }
    }
    std::cerr << "  passed\n";

    std::cerr << "Test verifyModuleResult - failure case\n";
    // Test verifyModuleResult with invalid module
    {
        il::core::Module badModule{};
        std::ostringstream discardErr;
        auto badLoad = il::tools::common::loadModuleFromFile(negativePath, badModule, discardErr);
        if (!badLoad.succeeded())
        {
            std::cerr << "  Failed to load bad module\n";
            return 1;
        }
        auto verifyResult = il::tools::common::verifyModuleResult(badModule);
        if (verifyResult.succeeded())
        {
            std::cerr << "  verifyModuleResult should have failed\n";
            return 1; // Should have failed verification
        }
        if (!verifyResult.isVerifyError())
        {
            std::cerr << "  verifyModuleResult should be VerifyError\n";
            return 1;
        }
        if (!verifyResult.diag.has_value())
        {
            std::cerr << "  verifyModuleResult should have diag\n";
            return 1;
        }
    }
    std::cerr << "  passed\n";

    std::cerr << "Test loadAndVerifyModule - success case\n";
    // Test loadAndVerifyModule - success case
    {
        il::core::Module combinedModule{};
        std::ostringstream combinedErr;
        auto result = il::tools::common::loadAndVerifyModule(
            (root / "tests/data/loop.il").string(), combinedModule, nullptr, combinedErr);
        if (!result.succeeded())
        {
            std::cerr << "  loadAndVerifyModule failed\n";
            return 1;
        }
        if (combinedErr.str().empty() == false)
        {
            std::cerr << "  Unexpected errors: " << combinedErr.str() << "\n";
            return 1; // Should not have errors
        }
    }
    std::cerr << "  passed\n";

    std::cerr << "Test loadAndVerifyModule - file error case\n";
    // Test loadAndVerifyModule - file error case
    {
        il::core::Module combinedModule{};
        std::ostringstream combinedErr;
        auto result = il::tools::common::loadAndVerifyModule(
            "/definitely/not/present.il", combinedModule, nullptr, combinedErr, "cannot open ");
        if (result.succeeded())
        {
            std::cerr << "  Should have failed\n";
            return 1; // Should have failed
        }
        if (!result.isFileError())
        {
            std::cerr << "  Should be FileError\n";
            return 1;
        }
        if (result.path != "/definitely/not/present.il")
        {
            std::cerr << "  Path mismatch: " << result.path << "\n";
            return 1;
        }
    }
    std::cerr << "  passed\n";

    std::cerr << "Test loadAndVerifyModule - parse error case\n";
    // Test loadAndVerifyModule - parse error case
    {
        il::core::Module combinedModule{};
        std::ostringstream combinedErr;
        auto result = il::tools::common::loadAndVerifyModule(
            (root / "tests/il/parse/mismatched_paren.il").string(),
            combinedModule,
            nullptr,
            combinedErr);
        if (result.succeeded())
        {
            std::cerr << "  Should have failed\n";
            return 1; // Should have failed
        }
        if (!result.isParseError())
        {
            std::cerr << "  Should be ParseError\n";
            return 1;
        }
    }
    std::cerr << "  passed\n";

    std::cerr << "Test loadAndVerifyModule - verify error case\n";
    // Test loadAndVerifyModule - verify error case
    {
        il::core::Module combinedModule{};
        std::ostringstream combinedErr;
        auto result = il::tools::common::loadAndVerifyModule(
            negativePath, combinedModule, nullptr, combinedErr);
        if (result.succeeded())
        {
            std::cerr << "  Should have failed verification\n";
            return 1; // Should have failed verification
        }
        if (!result.isVerifyError())
        {
            std::cerr << "  Should be VerifyError\n";
            return 1;
        }
        if (!result.diag.has_value())
        {
            std::cerr << "  Should have diag\n";
            return 1;
        }
    }
    std::cerr << "  passed\n";

    std::cerr << "Test printLoadResult - success\n";
    // Test printLoadResult - no output on success
    {
        il::tools::common::LoadResult successResult{
            il::tools::common::LoadStatus::Success, std::nullopt, ""};
        std::ostringstream printOut;
        il::tools::common::printLoadResult(successResult, printOut);
        if (!printOut.str().empty())
        {
            std::cerr << "  Should not print for success\n";
            return 1; // Should not print anything for success
        }
    }
    std::cerr << "  passed\n";

    std::cerr << "Test printLoadResult - file error\n";
    // Test printLoadResult - file error without diagnostic
    {
        il::tools::common::LoadResult fileErrResult{
            il::tools::common::LoadStatus::FileError, std::nullopt, "/some/path.il"};
        std::ostringstream printOut;
        il::tools::common::printLoadResult(fileErrResult, printOut);
        if (printOut.str().find("/some/path.il") == std::string::npos)
        {
            std::cerr << "  Should include path, got: " << printOut.str() << "\n";
            return 1; // Should include path in output
        }
    }
    std::cerr << "  passed\n";

    std::cerr << "Test statusName helper\n";
    // Test statusName helper
    {
        il::tools::common::LoadResult r;
        r.status = il::tools::common::LoadStatus::Success;
        if (std::string(r.statusName()) != "success")
        {
            std::cerr << "  Success statusName mismatch\n";
            return 1;
        }
        r.status = il::tools::common::LoadStatus::FileError;
        if (std::string(r.statusName()) != "file error")
        {
            std::cerr << "  FileError statusName mismatch\n";
            return 1;
        }
        r.status = il::tools::common::LoadStatus::ParseError;
        if (std::string(r.statusName()) != "parse error")
        {
            std::cerr << "  ParseError statusName mismatch\n";
            return 1;
        }
        r.status = il::tools::common::LoadStatus::VerifyError;
        if (std::string(r.statusName()) != "verify error")
        {
            std::cerr << "  VerifyError statusName mismatch\n";
            return 1;
        }
    }
    std::cerr << "  passed\n";

    std::cerr << "All tests passed!\n";
    return 0;
}
