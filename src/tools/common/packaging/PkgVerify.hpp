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

} // namespace viper::pkg
