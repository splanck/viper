// File: tests/tools/ModuleLoaderTests.cpp
// Purpose: Exercise the shared module loading helpers used by CLI tools.
// Key invariants: Helpers emit diagnostics on failure and succeed for valid inputs.
// Ownership/Lifetime: Test owns constructed modules and diagnostic streams.
// Links: docs/testing.md

#include "tools/common/module_loader.hpp"

#include <filesystem>
#include <sstream>

namespace
{
std::filesystem::path repoRoot()
{
    const auto sourcePath = std::filesystem::absolute(std::filesystem::path(__FILE__));
    return sourcePath.parent_path().parent_path().parent_path();
}
}

int main()
{
    const auto root = repoRoot();

    il::core::Module module{};
    std::ostringstream okErrors;
    auto okResult = il::tools::common::loadModuleFromFile((root / "tests/data/loop.il").string(), module, okErrors);
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
    auto missingResult = il::tools::common::loadModuleFromFile("/definitely/not/present.il", missingModule, missingErrors, "cannot open ");
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
    auto parseResult = il::tools::common::loadModuleFromFile((root / "tests/il/parse/mismatched_paren.il").string(), parseModule, parseErrors);
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
    auto negativeLoad = il::tools::common::loadModuleFromFile((root / "tests/il/negatives/unbalanced_eh.il").string(), verifyModule, verifyErrors);
    if (!negativeLoad.succeeded())
    {
        return 1;
    }
    if (!verifyErrors.str().empty())
    {
        return 1;
    }
    std::ostringstream verifyFail;
    if (il::tools::common::verifyModule(verifyModule, verifyFail))
    {
        return 1;
    }
    if (verifyFail.str().empty())
    {
        return 1;
    }

    return 0;
}

