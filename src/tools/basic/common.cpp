//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the small collection of helpers shared by BASIC command-line
// utilities. Centralising file-loading and diagnostic emission keeps the
// tool-specific entry points concise while ensuring error handling matches the
// long-standing behaviour documented for the BASIC frontend.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Contains reusable BASIC tool helpers for argument handling and I/O.
/// @details Each BASIC developer tool links this translation unit so they can
///          share usage text, file loading, and diagnostic messaging.

#include "tools/basic/common.hpp"

#include "support/source_manager.hpp"

#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>

namespace il::tools::basic
{

namespace
{
#ifdef VIPER_BASIC_TOOL_USAGE
constexpr const char *kUsageMessage = VIPER_BASIC_TOOL_USAGE;
#else
#error "VIPER_BASIC_TOOL_USAGE must be defined for BASIC tool builds"
#endif
} // namespace

/// @brief Load a BASIC source file and register it with a SourceManager.
///
/// @details The helper validates the provided command-line argument, prints
///          usage text when the argument is missing, and attempts to read the
///          requested file into @p buffer. Successfully loaded files are
///          registered with the supplied source manager so downstream lexer or
///          parser stages can resolve diagnostics back to the original path.
///          Failures leave @p buffer untouched so callers can reuse it.
///
/// @param path Filesystem path provided on the command line.
/// @param buffer Destination string that receives the file contents on success.
/// @param sm Source manager used to allocate a file identifier for the buffer.
/// @return File identifier when the load succeeds; `std::nullopt` when the
///         argument is missing or the file could not be opened.
std::optional<std::uint32_t> loadBasicSource(const char *path,
                                             std::string &buffer,
                                             il::support::SourceManager &sm)
{
    if (path == nullptr)
    {
        std::cerr << kUsageMessage;
        return std::nullopt;
    }

    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "cannot open " << path << "\n";
        return std::nullopt;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    std::string contents = ss.str();

    std::uint32_t fileId = sm.addFile(path);
    if (fileId == 0)
    {
        std::cerr << "cannot register " << path << "\n";
        return std::nullopt;
    }

    buffer = std::move(contents);
    return fileId;
}

} // namespace il::tools::basic
