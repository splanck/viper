//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/MachOCodeSign.cpp
// Purpose: Ad-hoc code signature generation for Mach-O arm64 executables.
//          Builds a CS_LINKER_SIGNED SuperBlob with SHA-256 page hashes.
// Key invariants:
//   - CS_LINKER_SIGNED flag required for macOS AMFI acceptance
//   - SHA-256 hash computed per 4KB page using CommonCrypto (macOS)
//   - CodeDirectory version 0x20400 (execSegBase/Limit/Flags)
// Ownership/Lifetime:
//   - Stateless builder — no persistent state
// Links: codegen/common/linker/MachOCodeSign.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/MachOCodeSign.hpp"
#include "codegen/common/linker/ExeWriterUtil.hpp"

#include <algorithm>
#include <cstring>

#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#endif

namespace viper::codegen::linker
{

using encoding::writeBE32;
using encoding::writeBE64;

namespace
{

/// Compute SHA-256 hash of data. Returns 32 bytes.
void sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
#if defined(__APPLE__)
    CC_SHA256(data, static_cast<CC_LONG>(len), out);
#else
    // Code signing only needed on macOS; zero-fill on other platforms.
    std::memset(out, 0, 32);
    (void)data;
    (void)len;
#endif
}

} // anonymous namespace

std::vector<uint8_t> buildCodeSignature(const std::vector<uint8_t> &file,
                                        size_t codeLimit,
                                        const std::string &identifier,
                                        uint64_t textSegFileOff,
                                        uint64_t textSegFileSize)
{
    // CodeDirectory constants (all big-endian in output).
    static constexpr uint32_t CSMAGIC_EMBEDDED_SIGNATURE = 0xFADE0CC0;
    static constexpr uint32_t CSMAGIC_CODEDIRECTORY = 0xFADE0C02;
    static constexpr uint32_t CSMAGIC_REQUIREMENTS = 0xFADE0C01;
    static constexpr uint32_t CS_ADHOC = 0x2;
    static constexpr uint32_t CS_LINKER_SIGNED = 0x20000;
    static constexpr uint32_t CS_EXECSEG_MAIN_BINARY = 0x1;
    static constexpr uint32_t CD_VERSION = 0x20400;

    const uint32_t nCodeSlots = static_cast<uint32_t>((codeLimit + 4095) / 4096);
    const size_t identLen = identifier.size() + 1; // including NUL
    const uint32_t cdHeaderSize = 88;              // version 0x20400 struct size
    const uint32_t hashOffset = static_cast<uint32_t>(cdHeaderSize + identLen);
    const uint32_t cdSize = hashOffset + nCodeSlots * 32;

    // --- Build CodeDirectory ---
    std::vector<uint8_t> cd;
    cd.reserve(cdSize);
    writeBE32(cd, CSMAGIC_CODEDIRECTORY);
    writeBE32(cd, cdSize);
    writeBE32(cd, CD_VERSION);
    writeBE32(cd, CS_ADHOC | CS_LINKER_SIGNED);
    writeBE32(cd, hashOffset);   // hashOffset
    writeBE32(cd, cdHeaderSize); // identOffset
    writeBE32(cd, 0);            // nSpecialSlots
    writeBE32(cd, nCodeSlots);
    writeBE32(cd, static_cast<uint32_t>(codeLimit));
    cd.push_back(32);                      // hashSize (SHA-256)
    cd.push_back(2);                       // hashType (SHA-256)
    cd.push_back(0);                       // platform
    cd.push_back(12);                      // pageSize (log2 4096)
    writeBE32(cd, 0);                      // spare2
    writeBE32(cd, 0);                      // scatterOffset (v >= 0x20100)
    writeBE32(cd, 0);                      // teamOffset (v >= 0x20200)
    writeBE32(cd, 0);                      // spare3 (v >= 0x20300)
    writeBE64(cd, codeLimit);              // codeLimit64
    writeBE64(cd, textSegFileOff);         // execSegBase (v >= 0x20400)
    writeBE64(cd, textSegFileSize);        // execSegLimit
    writeBE64(cd, CS_EXECSEG_MAIN_BINARY); // execSegFlags

    // Identifier string.
    cd.insert(cd.end(), identifier.begin(), identifier.end());
    cd.push_back(0);

    // Code slot hashes: SHA-256 of each 4KB page.
    for (uint32_t i = 0; i < nCodeSlots; ++i)
    {
        size_t pageStart = static_cast<size_t>(i) * 4096;
        size_t pageEnd = std::min(pageStart + 4096, codeLimit);
        uint8_t hash[32];
        sha256(file.data() + pageStart, pageEnd - pageStart, hash);
        cd.insert(cd.end(), hash, hash + 32);
    }

    // --- Build empty Requirements blob ---
    std::vector<uint8_t> req;
    writeBE32(req, CSMAGIC_REQUIREMENTS);
    writeBE32(req, 12);
    writeBE32(req, 0); // count = 0

    // --- Build SuperBlob ---
    const uint32_t sbHeaderSize = 12 + 2 * 8; // header(12) + 2 index entries(8 each)
    const uint32_t sbSize = sbHeaderSize + static_cast<uint32_t>(cd.size() + req.size());

    std::vector<uint8_t> sb;
    sb.reserve(sbSize);
    writeBE32(sb, CSMAGIC_EMBEDDED_SIGNATURE);
    writeBE32(sb, sbSize);
    writeBE32(sb, 2); // count = 2 blobs

    // Index entry 0: CodeDirectory (type = 0)
    writeBE32(sb, 0);
    writeBE32(sb, sbHeaderSize);

    // Index entry 1: Requirements (type = 2)
    writeBE32(sb, 2);
    writeBE32(sb, sbHeaderSize + static_cast<uint32_t>(cd.size()));

    sb.insert(sb.end(), cd.begin(), cd.end());
    sb.insert(sb.end(), req.begin(), req.end());
    return sb;
}

} // namespace viper::codegen::linker
