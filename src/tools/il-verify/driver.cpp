//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the reusable verification pipeline behind the `il-verify`
// executable.  The helper assembles the module loader, verifier, and reporting
// logic so that both the CLI and tests can invoke the same behaviour while
// injecting a preconfigured SourceManager.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the shared verification helper for the `il-verify` tool.
/// @details The helper stores the SourceManager-assigned file identifier and
///          propagates exhaustion failures immediately, mirroring the CLI's
///          user-visible behaviour while remaining testable without invoking
///          `main`.

#include "tools/il-verify/driver.hpp"

#include "il/core/Module.hpp"
#include "tools/common/module_loader.hpp"

#include <ostream>
#include <string>

namespace il::tools::verify
{

bool runVerificationPipeline(std::string_view path,
                             std::ostream &out,
                             std::ostream &err,
                             il::support::SourceManager &sm)
{
    il::core::Module module;
    const std::string pathStr(path);

    const uint32_t fileId = sm.addFile(pathStr);
    if (fileId == 0)
    {
        return false;
    }

    auto load = il::tools::common::loadModuleFromFile(pathStr, module, err, "cannot open ");
    if (!load.succeeded())
    {
        return false;
    }

    if (!il::tools::common::verifyModule(module, err, &sm))
    {
        return false;
    }

    out << "OK\n";
    return true;
}

} // namespace il::tools::verify

