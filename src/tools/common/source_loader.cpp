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

namespace il::tools::common {

il::support::Expected<LoadedSource> loadSourceBuffer(const std::string &path,
                                                     il::support::SourceManager &sm) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return il::support::Expected<LoadedSource>(il::support::Diagnostic{
            il::support::Severity::Error, "unable to open " + path, {}, {}});
    }

    // Check file size before reading to avoid OOM on huge files.
    in.seekg(0, std::ios::end);
    auto fileSize = in.tellg();
    in.seekg(0, std::ios::beg);
    constexpr auto kMaxSourceSize = static_cast<std::streamoff>(256ULL * 1024 * 1024);
    if (fileSize < 0 || fileSize > kMaxSourceSize) {
        return il::support::Expected<LoadedSource>(
            il::support::Diagnostic{il::support::Severity::Error,
                                    "source file too large: " + path + " (limit: 256 MB)",
                                    {},
                                    {}});
    }

    try {
        std::string contents(static_cast<std::size_t>(fileSize), '\0');
        if (fileSize > 0)
            in.read(contents.data(), fileSize);
        if (!in) {
            return il::support::Expected<LoadedSource>(il::support::Diagnostic{
                il::support::Severity::Error, "read error reading " + path, {}, {}});
        }

        const uint32_t fileId = sm.addFile(path);
        if (fileId == 0) {
            return il::support::Expected<LoadedSource>(il::support::makeErrorWithCode(
                {},
                "V-SRC-FILE-ID",
                std::string{il::support::kSourceManagerFileIdOverflowMessage}));
        }

        LoadedSource source{};
        source.buffer = std::move(contents);
        source.fileId = fileId;
        return il::support::Expected<LoadedSource>(std::move(source));
    } catch (const std::bad_alloc &) {
        return il::support::Expected<LoadedSource>(il::support::Diagnostic{
            il::support::Severity::Error, "out of memory reading " + path, {}, {}});
    }
}

il::support::Expected<std::string> loadSourceFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return il::support::Expected<std::string>(il::support::Diagnostic{
            il::support::Severity::Error, "unable to open " + path, {}, {}});
    }

    in.seekg(0, std::ios::end);
    auto fileSize = in.tellg();
    in.seekg(0, std::ios::beg);
    constexpr auto kMaxSourceSize = static_cast<std::streamoff>(256ULL * 1024 * 1024);
    if (fileSize < 0 || fileSize > kMaxSourceSize) {
        return il::support::Expected<std::string>(
            il::support::Diagnostic{il::support::Severity::Error,
                                    "source file too large: " + path + " (limit: 256 MB)",
                                    {},
                                    {}});
    }

    try {
        std::string contents(static_cast<std::size_t>(fileSize), '\0');
        if (fileSize > 0)
            in.read(contents.data(), fileSize);
        if (!in) {
            return il::support::Expected<std::string>(il::support::Diagnostic{
                il::support::Severity::Error, "read error reading " + path, {}, {}});
        }
        return il::support::Expected<std::string>(std::move(contents));
    } catch (const std::bad_alloc &) {
        return il::support::Expected<std::string>(il::support::Diagnostic{
            il::support::Severity::Error, "out of memory reading " + path, {}, {}});
    }
}

} // namespace il::tools::common
