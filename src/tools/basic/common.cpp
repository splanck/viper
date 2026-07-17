//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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
// Links: docs/internals/codemap.md#tools, src/tools/basic/common.hpp
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Contains reusable BASIC tool helpers for argument handling and I/O.
/// @details Each BASIC developer tool links this translation unit so they can
///          share usage text, file loading, and diagnostic messaging.

#include "tools/basic/common.hpp"

#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include "tools/common/source_loader.hpp"

#include <iostream>
#include <optional>
#include <sstream>

namespace il::tools::basic {

namespace {
#ifdef ZANNA_BASIC_TOOL_USAGE
constexpr const char *kUsageMessage = ZANNA_BASIC_TOOL_USAGE;
#else
#error "ZANNA_BASIC_TOOL_USAGE must be defined for BASIC tool builds"
#endif
} // namespace

/// @brief Load a BASIC source file and register it with a SourceManager.
///
/// @details The helper performs the following workflow:
///          1. Validate @p path and emit the shared usage text when no argument
///             was supplied, allowing callers to exit early with a consistent
///             message.
///          2. Reject files larger than the 256 MB limit (or with an
///             unmeasurable size) before any allocation, guarding against
///             out-of-memory conditions on pathological inputs.
///          3. Read the bytes into a pre-sized buffer in a single read,
///             reporting an "incomplete read" diagnostic on a short read and an
///             out-of-memory diagnostic if the allocation throws @c std::bad_alloc.
///          4. Register the path with the provided @ref il::support::SourceManager
///             so downstream diagnostics can resolve the file identifier back to
///             the textual path.
///          5. Copy the buffered contents into @p buffer only after the previous
///             steps have succeeded, leaving the caller's storage untouched when
///             failures occur.
///          Errors while opening the file, exceeding the size limit, or
///          registering the path are reported to @c std::cerr with human-readable
///          messages.  The function returns an engaged optional only when the
///          caller can safely proceed with compilation.
///
/// @param path Filesystem path provided on the command line.
/// @param buffer Destination string that receives the file contents on success.
/// @param sm Source manager used to allocate a file identifier for the buffer.
/// @return File identifier when the load succeeds; `std::nullopt` on error.
std::optional<std::uint32_t> loadBasicSource(const char *path,
                                             std::string &buffer,
                                             il::support::SourceManager &sm) {
    if (path == nullptr) {
        std::cerr << kUsageMessage;
        return std::nullopt;
    }

    auto loaded = il::tools::common::loadSourceBuffer(path, sm);
    if (!loaded) {
        il::support::printDiag(loaded.error(), std::cerr, &sm);
        return std::nullopt;
    }

    const std::uint32_t fileId = loaded.value().fileId;
    buffer = std::move(loaded.value().buffer);
    return fileId;
}

} // namespace il::tools::basic
