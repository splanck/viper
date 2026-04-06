//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/MachOCodeSign.hpp
// Purpose: Ad-hoc code signature generation for Mach-O arm64 executables.
//          Builds an Apple-compatible embedded signature SuperBlob with
//          SHA-256 page hashes.
// Key invariants:
//   - Matches Apple's ad-hoc arm64 layout: CodeDirectory + Requirements +
//     empty BlobWrapper
//   - SHA-256 hash computed per target page size
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

namespace viper::codegen::linker {

/// Return the exact embedded signature size for a Mach-O executable.
size_t estimateCodeSignatureSize(size_t codeLimit,
                                 const std::string &identifier,
                                 size_t pageSize);

/// Build an ad-hoc code signature (SuperBlob) for a Mach-O executable.
/// Returns the complete code signature blob to append to __LINKEDIT.
///
/// The signature includes:
///   - SuperBlob (CSMAGIC_EMBEDDED_SIGNATURE) containing:
///     - CodeDirectory (CSMAGIC_CODEDIRECTORY v0x20400) with CS_ADHOC flag
///     - Empty Requirements blob (CSMAGIC_REQUIREMENTS)
///     - Empty BlobWrapper (0xFADE0B01) in signature slot 0x10000
///   - Requirements hash in special slot -2
///   - Zero hash in special slot -1 (no Info.plist)
///   - SHA-256 hash of each page up to codeLimit
std::vector<uint8_t> buildCodeSignature(const std::vector<uint8_t> &file,
                                        size_t codeLimit,
                                        const std::string &identifier,
                                        uint64_t textSegFileOff,
                                        uint64_t textSegFileSize,
                                        size_t pageSize);

} // namespace viper::codegen::linker
