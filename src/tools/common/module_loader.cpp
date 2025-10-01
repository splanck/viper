// File: src/tools/common/module_loader.cpp
// Purpose: Implement reusable helpers for loading and verifying IL modules in CLI tools.
// Key invariants: Diagnostics are emitted exactly once per failure and mirror existing tool messaging.
// Ownership/Lifetime: Streams and modules are owned by the caller; this file performs transient operations only.
// Links: docs/codemap.md

#include "tools/common/module_loader.hpp"

#include "il/api/expected_api.hpp"

#include <fstream>

namespace il::tools::common
{
namespace
{
LoadResult makeSuccess()
{
    return { LoadStatus::Success, std::nullopt };
}

LoadResult makeFileError()
{
    return { LoadStatus::FileError, std::nullopt };
}

LoadResult makeParseError(const il::support::Diag &diag)
{
    return { LoadStatus::ParseError, diag };
}
} // namespace

LoadResult loadModuleFromFile(const std::string &path,
                              il::core::Module &module,
                              std::ostream &err,
                              std::string_view ioErrorPrefix)
{
    std::ifstream input(path);
    if (!input)
    {
        err << ioErrorPrefix << path << '\n';
        return makeFileError();
    }

    auto parsed = il::api::v2::parse_text_expected(input, module);
    if (!parsed)
    {
        const auto diag = parsed.error();
        il::support::printDiag(diag, err);
        return makeParseError(diag);
    }

    return makeSuccess();
}

bool verifyModule(const il::core::Module &module, std::ostream &err)
{
    auto verified = il::api::v2::verify_module_expected(module);
    if (!verified)
    {
        il::support::printDiag(verified.error(), err);
        return false;
    }
    return true;
}

} // namespace il::tools::common

