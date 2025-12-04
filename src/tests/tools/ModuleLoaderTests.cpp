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
#include <sstream>

namespace
{
std::filesystem::path repoRoot()
{
    const auto sourcePath = std::filesystem::absolute(std::filesystem::path(__FILE__));
    return sourcePath.parent_path().parent_path().parent_path();
}
} // namespace

int main()
{
    const auto root = repoRoot();

    il::core::Module module{};
    std::ostringstream okErrors;
    auto okResult = il::tools::common::loadModuleFromFile(
        (root / "tests/data/loop.il").string(), module, okErrors);
    if (!okResult.succeeded())
    {
        return 1;
    }
    if (!okErrors.str().empty())
    {
        return 1;
    }
    std::ostringstream verifyOk;
    if (!il::tools::common::verifyModule(module, verifyOk))
    {
        return 1;
    }
    if (!verifyOk.str().empty())
    {
        return 1;
    }

    il::core::Module missingModule{};
    std::ostringstream missingErrors;
    auto missingResult = il::tools::common::loadModuleFromFile(
        "/definitely/not/present.il", missingModule, missingErrors, "cannot open ");
    if (missingResult.status != il::tools::common::LoadStatus::FileError)
    {
        return 1;
    }
    if (missingErrors.str() != "cannot open /definitely/not/present.il\n")
    {
        return 1;
    }

    il::core::Module parseModule{};
    std::ostringstream parseErrors;
    auto parseResult = il::tools::common::loadModuleFromFile(
        (root / "tests/il/parse/mismatched_paren.il").string(), parseModule, parseErrors);
    if (parseResult.status != il::tools::common::LoadStatus::ParseError)
    {
        return 1;
    }
    if (parseErrors.str().empty())
    {
        return 1;
    }

    il::core::Module verifyModule{};
    std::ostringstream verifyErrors;
    const auto negativePath = (root / "tests/il/negatives/unbalanced_eh.il").string();
    il::support::SourceManager sm;
    const auto fileId = sm.addFile(negativePath);
    auto negativeLoad =
        il::tools::common::loadModuleFromFile(negativePath, verifyModule, verifyErrors);
    if (!negativeLoad.succeeded())
    {
        return 1;
    }
    if (!verifyErrors.str().empty())
    {
        return 1;
    }
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
    std::ostringstream verifyFail;
    if (il::tools::common::verifyModule(verifyModule, verifyFail, &sm))
    {
        return 1;
    }
    const std::string diag = verifyFail.str();
    if (diag.empty())
    {
        return 1;
    }
    const std::string prefix = negativePath + ":";
    if (diag.rfind(prefix, 0) != 0)
    {
        return 1;
    }
    const auto lineStart = prefix.size();
    const auto lineEnd = diag.find(':', lineStart);
    if (lineEnd == std::string::npos)
    {
        return 1;
    }
    const auto lineStr = diag.substr(lineStart, lineEnd - lineStart);
    auto isDigit = [](unsigned char ch) { return std::isdigit(ch) != 0; };
    if (lineStr.empty() || !std::all_of(lineStr.begin(), lineStr.end(), isDigit))
    {
        return 1;
    }
    const auto columnStart = lineEnd + 1;
    const auto columnEnd = diag.find(':', columnStart);
    if (columnEnd == std::string::npos)
    {
        return 1;
    }
    const auto columnStr = diag.substr(columnStart, columnEnd - columnStart);
    if (columnStr.empty() || !std::all_of(columnStr.begin(), columnStr.end(), isDigit))
    {
        return 1;
    }

    // Test verifyModuleResult - returns LoadResult instead of bool
    {
        il::core::Module goodModule{};
        std::ostringstream discardErr;
        auto goodLoad = il::tools::common::loadModuleFromFile(
            (root / "tests/data/loop.il").string(), goodModule, discardErr);
        if (!goodLoad.succeeded())
        {
            return 1;
        }
        auto verifyResult = il::tools::common::verifyModuleResult(goodModule);
        if (!verifyResult.succeeded())
        {
            return 1;
        }
        if (verifyResult.diag.has_value())
        {
            return 1;
        }
    }

    // Test verifyModuleResult with invalid module
    {
        il::core::Module badModule{};
        std::ostringstream discardErr;
        auto badLoad = il::tools::common::loadModuleFromFile(negativePath, badModule, discardErr);
        if (!badLoad.succeeded())
        {
            return 1;
        }
        auto verifyResult = il::tools::common::verifyModuleResult(badModule);
        if (verifyResult.succeeded())
        {
            return 1; // Should have failed verification
        }
        if (!verifyResult.isVerifyError())
        {
            return 1;
        }
        if (!verifyResult.diag.has_value())
        {
            return 1;
        }
    }

    // Test loadAndVerifyModule - success case
    {
        il::core::Module combinedModule{};
        std::ostringstream combinedErr;
        auto result = il::tools::common::loadAndVerifyModule(
            (root / "tests/data/loop.il").string(), combinedModule, nullptr, combinedErr);
        if (!result.succeeded())
        {
            return 1;
        }
        if (combinedErr.str().empty() == false)
        {
            return 1; // Should not have errors
        }
    }

    // Test loadAndVerifyModule - file error case
    {
        il::core::Module combinedModule{};
        std::ostringstream combinedErr;
        auto result = il::tools::common::loadAndVerifyModule(
            "/definitely/not/present.il", combinedModule, nullptr, combinedErr, "cannot open ");
        if (result.succeeded())
        {
            return 1; // Should have failed
        }
        if (!result.isFileError())
        {
            return 1;
        }
        if (result.path != "/definitely/not/present.il")
        {
            return 1;
        }
    }

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
            return 1; // Should have failed
        }
        if (!result.isParseError())
        {
            return 1;
        }
    }

    // Test loadAndVerifyModule - verify error case
    {
        il::core::Module combinedModule{};
        std::ostringstream combinedErr;
        auto result = il::tools::common::loadAndVerifyModule(
            negativePath, combinedModule, nullptr, combinedErr);
        if (result.succeeded())
        {
            return 1; // Should have failed verification
        }
        if (!result.isVerifyError())
        {
            return 1;
        }
        if (!result.diag.has_value())
        {
            return 1;
        }
    }

    // Test printLoadResult - no output on success
    {
        il::tools::common::LoadResult successResult{
            il::tools::common::LoadStatus::Success, std::nullopt, ""};
        std::ostringstream printOut;
        il::tools::common::printLoadResult(successResult, printOut);
        if (!printOut.str().empty())
        {
            return 1; // Should not print anything for success
        }
    }

    // Test printLoadResult - file error without diagnostic
    {
        il::tools::common::LoadResult fileErrResult{
            il::tools::common::LoadStatus::FileError, std::nullopt, "/some/path.il"};
        std::ostringstream printOut;
        il::tools::common::printLoadResult(fileErrResult, printOut);
        if (printOut.str().find("/some/path.il") == std::string::npos)
        {
            return 1; // Should include path in output
        }
    }

    // Test statusName helper
    {
        il::tools::common::LoadResult r;
        r.status = il::tools::common::LoadStatus::Success;
        if (std::string(r.statusName()) != "success")
        {
            return 1;
        }
        r.status = il::tools::common::LoadStatus::FileError;
        if (std::string(r.statusName()) != "file error")
        {
            return 1;
        }
        r.status = il::tools::common::LoadStatus::ParseError;
        if (std::string(r.statusName()) != "parse error")
        {
            return 1;
        }
        r.status = il::tools::common::LoadStatus::VerifyError;
        if (std::string(r.statusName()) != "verify error")
        {
            return 1;
        }
    }

    return 0;
}
