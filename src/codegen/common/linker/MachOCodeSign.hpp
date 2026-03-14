//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/MachOCodeSign.hpp
// Purpose: Ad-hoc code signature generation for Mach-O arm64 executables.
//          Builds a CS_LINKER_SIGNED SuperBlob with SHA-256 page hashes.
// Key invariants:
//   - CS_LINKER_SIGNED flag required for macOS AMFI acceptance
//   - SHA-256 hash computed per 4KB page
//   - CodeDirectory version 0x20400 (execSegBase/Limit/Flags)
// Ownership/Lifetime:
//   - Stateless builder — returns owned blob
// Links: codegen/common/linker/MachOExeWriter.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace viper::codegen::linker
{

/// Build a linker-signed ad-hoc code signature (SuperBlob) for a Mach-O executable.
/// Returns the complete code signature blob to append to __LINKEDIT.
///
/// The signature includes:
///   - SuperBlob (CSMAGIC_EMBEDDED_SIGNATURE) containing:
///     - CodeDirectory (CSMAGIC_CODEDIRECTORY v0x20400) with CS_LINKER_SIGNED flag
///     - Empty Requirements blob (CSMAGIC_REQUIREMENTS)
///   - SHA-256 hash of each 4KB page up to codeLimit
///
/// The CS_LINKER_SIGNED flag is critical: macOS AMFI rejects ad-hoc binaries
/// without it. Only linkers are supposed to set this flag (codesign doesn't).
std::vector<uint8_t> buildCodeSignature(const std::vector<uint8_t> &file,
                                        size_t codeLimit,
                                        const std::string &identifier,
                                        uint64_t textSegFileOff,
                                        uint64_t textSegFileSize);

} // namespace viper::codegen::linker
