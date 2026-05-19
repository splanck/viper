//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PkgVerify.hpp
// Purpose: Post-build verification of generated packages — checks structural
//          correctness of ZIP, .deb (ar), and PE files after creation.
//
// Key invariants:
//   - All functions return true on success, false on structural error.
//   - Errors are reported to the provided ostream.
//   - Read-only verification — no modifications to the input data.
//
// Ownership/Lifetime:
//   - Pure functions operating on in-memory byte vectors.
//
// Links: ZipWriter.hpp, ArWriter.hpp, PEBuilder.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace viper::pkg {

/// @brief Verify structural correctness of a ZIP archive.
///
/// Checks:
///   - End-of-central-directory signature present
///   - Entry count matches central directory
///   - Local header signatures valid
///
/// @param data ZIP file bytes.
/// @param err  Stream for error messages.
/// @return true if the ZIP is structurally valid.
bool verifyZip(const std::vector<uint8_t> &data, std::ostream &err);

/// @brief Verify a macOS .app ZIP contains the required app payload files.
bool verifyMacOSAppZip(const std::vector<uint8_t> &data,
                       const std::string &appBundleName,
                       const std::string &executableName,
                       std::ostream &err);

/// @brief Verify structural correctness of a .deb (ar) archive.
///
/// Checks:
///   - ar magic "!<arch>\n" present
///   - First member is "debian-binary" with content "2.0\n"
///   - "control.tar.gz" and "data.tar.gz" members present
///
/// @param data .deb file bytes.
/// @param err  Stream for error messages.
/// @return true if the .deb is structurally valid.
bool verifyDeb(const std::vector<uint8_t> &data, std::ostream &err);

/// @brief Verify a .deb archive and assert required data.tar payload paths.
bool verifyDebPayload(const std::vector<uint8_t> &data,
                      const std::vector<std::string> &requiredPaths,
                      std::ostream &err);

/// @brief Verify a gzip-compressed USTAR tarball.
///
/// Checks:
///   - gzip framing, CRC-32, and uncompressed size
///   - tar headers, checksums, end markers, and safe relative paths
///
/// @param data .tar.gz file bytes.
/// @param err  Stream for error messages.
/// @return true if the tarball is structurally valid.
bool verifyTarGz(const std::vector<uint8_t> &data, std::ostream &err);

/// @brief Verify a .tar.gz archive and assert required payload paths.
bool verifyTarGzPayload(const std::vector<uint8_t> &data,
                        const std::vector<std::string> &requiredPaths,
                        std::ostream &err);

/// @brief Verify structural correctness of a portable ASCII or newc CPIO archive.
bool verifyCpioNewc(const std::vector<uint8_t> &data, std::ostream &err);

/// @brief Verify a CPIO archive and assert required payload paths.
bool verifyCpioNewcPayload(const std::vector<uint8_t> &data,
                           const std::vector<std::string> &requiredPaths,
                           std::ostream &err);

/// @brief Verify structural correctness of a XAR archive.
bool verifyXar(const std::vector<uint8_t> &data, std::ostream &err);

/// @brief Verify a macOS flat `.pkg` archive.
bool verifyMacOSPkg(const std::vector<uint8_t> &data, std::ostream &err);

/// @brief Verify a macOS flat `.pkg` archive and assert payload paths.
bool verifyMacOSPkgPayload(const std::vector<uint8_t> &data,
                           const std::vector<std::string> &requiredPaths,
                           std::ostream &err);

/// @brief Verify structural correctness of a PE32+ executable.
///
/// Checks:
///   - DOS header "MZ" magic
///   - PE signature "PE\0\0" at e_lfanew
///   - Section headers non-overlapping
///
/// @param data PE file bytes.
/// @param err  Stream for error messages.
/// @return true if the PE is structurally valid.
bool verifyPE(const std::vector<uint8_t> &data, std::ostream &err);

/// @brief Verify a PE32+ executable that is expected to carry a ZIP overlay.
///
/// Checks:
///   - Base PE structure is valid
///   - Overlay exists after the final section payload
///   - Overlay bytes form a structurally valid ZIP archive
///
/// @param data PE file bytes.
/// @param err  Stream for error messages.
/// @return true if the PE and its ZIP overlay are structurally valid.
bool verifyPEZipOverlay(const std::vector<uint8_t> &data, std::ostream &err);

/// @brief Verify a PE ZIP overlay and assert required ZIP entries.
bool verifyPEZipOverlayPayload(const std::vector<uint8_t> &data,
                               const std::vector<std::string> &requiredEntries,
                               std::ostream &err);

/// @brief Verify a PE ZIP overlay plus a nested ZIP entry payload.
bool verifyPEZipOverlayNestedPayload(const std::vector<uint8_t> &data,
                                     const std::vector<std::string> &requiredOuterEntries,
                                     const std::string &innerZipEntry,
                                     const std::vector<std::string> &requiredInnerEntries,
                                     std::ostream &err);

} // namespace viper::pkg
