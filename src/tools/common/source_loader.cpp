//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/source_loader.cpp
// Purpose: Standardise how command-line tools load source files into memory.
// Key invariants: The loaded buffer contains the complete file contents.
// Ownership/Lifetime: The returned LoadedSource owns its buffer; the caller
//                     may use it after the function returns.
// Links: src/tools/common/source_loader.hpp, docs/codemap.md#tools
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Provides source file loading helpers for frontend CLI tools.
/// @details Language frontends link this implementation to standardise how
///          source files are read into memory and registered with the
///          SourceManager for diagnostic reporting.

#include "tools/common/source_loader.hpp"

#include <fstream>
#include <sstream>

namespace il::tools::common
{

il::support::Expected<LoadedSource> loadSourceBuffer(const std::string &path,
                                                     il::support::SourceManager &sm)
{
    std::ifstream in(path);
    if (!in)
    {
        return il::support::Expected<LoadedSource>(il::support::Diagnostic{
            il::support::Severity::Error, "unable to open " + path, {}, {}});
    }

    std::ostringstream ss;
    ss << in.rdbuf();

    std::string contents = ss.str();

    const uint32_t fileId = sm.addFile(path);
    if (fileId == 0)
    {
        return il::support::Expected<LoadedSource>(il::support::makeError(
            {}, std::string{il::support::kSourceManagerFileIdOverflowMessage}));
    }

    LoadedSource source{};
    source.buffer = std::move(contents);
    source.fileId = fileId;
    return il::support::Expected<LoadedSource>(std::move(source));
}

il::support::Expected<std::string> loadSourceFile(const std::string &path)
{
    std::ifstream in(path);
    if (!in)
    {
        return il::support::Expected<std::string>(il::support::Diagnostic{
            il::support::Severity::Error, "unable to open " + path, {}, {}});
    }

    std::ostringstream ss;
    ss << in.rdbuf();

    return il::support::Expected<std::string>(ss.str());
}

} // namespace il::tools::common
