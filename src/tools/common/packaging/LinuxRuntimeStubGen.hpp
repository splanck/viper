//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/LinuxRuntimeStubGen.hpp
// Purpose: Generate the Linux self-extracting AppImage runtime stub.
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

namespace viper::pkg {

/// Marker line separating the runtime stub from the appended AppImage payload.
constexpr const char *kLinuxRuntimePayloadMarker = "__VIPER_APPIMAGE_PAYLOAD_BELOW__";

/// Parameters for the Linux AppImage runtime stub.
struct LinuxRuntimeStubParams {
    std::string cacheName; ///< Stable cache directory name, e.g. "viper-1.2.3-x64".
    std::string entryPath; ///< Payload-relative executable path, e.g. "bin/viper".
    std::string payloadSha256; ///< Optional 64-char SHA-256 digest for the appended tar.gz payload.
};

/// Build the self-extracting Linux runtime stub bytes.
std::vector<uint8_t> buildLinuxRuntimeStub(const LinuxRuntimeStubParams &params);

/// Build a complete self-extracting AppImage from a gzip-compressed tar payload.
std::vector<uint8_t> buildLinuxAppImage(const LinuxRuntimeStubParams &params,
                                        const std::vector<uint8_t> &payloadTarGz);

/// Verify the basic self-extracting AppImage layout.
bool verifyLinuxAppImage(const std::vector<uint8_t> &data, std::string *err = nullptr);

} // namespace viper::pkg
