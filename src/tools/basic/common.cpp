//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/basic/common.cpp
// Purpose: Provide shared command-line helpers used by BASIC developer tools.
// Key invariants: Usage messages remain consistent across tools by reusing the
//                 same usage macro; successful file loads always register a
//                 source-manager entry before returning the identifier to the
//                 caller.
// Ownership/Lifetime: The helper borrows the caller-provided string buffer and
//                     source manager, storing file contents directly into the
//                     buffer so ownership never escapes the tool-specific
//                     entrypoint.
// Links: docs/codemap.md#tools, src/tools/basic/common.hpp
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
/// @details The helper performs the following workflow:
///          1. Validate @p path and emit the shared usage text when no argument
///             was supplied, allowing callers to exit early with a consistent
///             message.
///          2. Stream the file contents into an @ref std::ostringstream to
///             preserve original newlines and avoid partial reads.
///          3. Register the path with the provided @ref il::support::SourceManager
///             so downstream diagnostics can resolve the file identifier back to
///             the textual path.
///          4. Copy the buffered contents into @p buffer only after the previous
///             steps have succeeded, leaving the caller's storage untouched when
///             failures occur.
///          Errors while opening the file or registering the path are reported to
///          @c std::cerr with human-readable messages.  The function returns an
///          engaged optional only when the caller can safely proceed with
///          compilation.
///
/// @param path Filesystem path provided on the command line.
/// @param buffer Destination string that receives the file contents on success.
/// @param sm Source manager used to allocate a file identifier for the buffer.
/// @return File identifier when the load succeeds; `std::nullopt` on error.
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
    const std::string contents = ss.str();

    std::uint32_t fileId = sm.addFile(path);
    if (fileId == 0)
    {
        std::cerr << "cannot register " << path << "\n";
        return std::nullopt;
    }

    buffer = contents;
    return fileId;
}

} // namespace il::tools::basic
