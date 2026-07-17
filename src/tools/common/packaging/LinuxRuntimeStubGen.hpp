//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/LinuxRuntimeStubGen.hpp
// Purpose: Generate the Zanna self-extracting Linux bundle runtime stub.
//
// Key invariants:
//   - The payload is appended after a marker line and is a gzip-compressed tar archive.
//   - The generated file is executable without FUSE or squashfs support.
//   - Extraction prefers XDG_CACHE_HOME, then TMPDIR, then /tmp.
//
// Ownership/Lifetime:
//   - Output returned as a byte vector; callers append the payload bytes.
//
// Links: LinuxPackageBuilder.cpp, TarWriter.hpp, PkgGzip.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zanna::pkg {

/// Marker line separating the runtime stub from the appended Linux bundle payload.
constexpr const char *kLinuxRuntimePayloadMarker = "__ZANNA_APPIMAGE_PAYLOAD_BELOW__";

/// Parameters for the self-extracting Linux bundle runtime stub.
struct LinuxRuntimeStubParams {
    std::string cacheName;     ///< Stable bundle cache prefix, e.g. "zanna-1.2.3-x64".
    std::string entryPath;     ///< Payload-relative executable path, e.g. "bin/zanna".
    std::string payloadSha256; ///< Optional 64-char SHA-256 digest for the appended tar.gz payload.
    bool appImageInterface{false}; ///< Expose AppImage-named flags for real application AppImages.
};

/// Build the self-extracting Linux bundle runtime stub bytes.
std::vector<uint8_t> buildLinuxRuntimeStub(const LinuxRuntimeStubParams &params);

/// Build a complete self-extracting Linux bundle from a gzip-compressed tar payload.
std::vector<uint8_t> buildLinuxAppImage(const LinuxRuntimeStubParams &params,
                                        const std::vector<uint8_t> &payloadTarGz);

/// Verify the basic self-extracting Linux bundle layout.
bool verifyLinuxAppImage(const std::vector<uint8_t> &data, std::string *err = nullptr);

} // namespace zanna::pkg
