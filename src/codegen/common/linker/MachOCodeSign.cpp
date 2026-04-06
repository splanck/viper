//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/MachOCodeSign.cpp
// Purpose: Ad-hoc code signature generation for Mach-O arm64 executables.
//          Builds an Apple-compatible embedded signature SuperBlob with
//          SHA-256 page hashes.
// Key invariants:
//   - Matches Apple's ad-hoc layout: CodeDirectory + Requirements +
//     empty BlobWrapper
//   - SHA-256 hash computed per target page size using CommonCrypto (macOS)
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

namespace viper::codegen::linker {

using encoding::writeBE32;
using encoding::writeBE64;

namespace {

static constexpr uint32_t CSMAGIC_EMBEDDED_SIGNATURE = 0xFADE0CC0;
static constexpr uint32_t CSMAGIC_CODEDIRECTORY = 0xFADE0C02;
static constexpr uint32_t CSMAGIC_REQUIREMENTS = 0xFADE0C01;
static constexpr uint32_t CSMAGIC_BLOBWRAPPER = 0xFADE0B01;
static constexpr uint32_t CSSLOT_CODEDIRECTORY = 0;
static constexpr uint32_t CSSLOT_REQUIREMENTS = 2;
static constexpr uint32_t CSSLOT_SIGNATURESLOT = 0x10000;
static constexpr uint32_t CS_ADHOC = 0x2;
static constexpr uint32_t CS_EXECSEG_MAIN_BINARY = 0x1;
static constexpr uint32_t CD_VERSION = 0x20400;
static constexpr uint32_t kCodeDirectoryHeaderSize = 88;
static constexpr uint32_t kHashSize = 32;
static constexpr uint32_t kSpecialSlots = 2;
static constexpr uint32_t kRequirementsSize = 12;
static constexpr uint32_t kBlobWrapperSize = 8;
static constexpr uint32_t kSuperBlobHeaderSize = 12 + 3 * 8;

/// Compute SHA-256 hash of data. Returns 32 bytes.
void sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
#if defined(__APPLE__)
    CC_SHA256(data, static_cast<CC_LONG>(len), out);
#else
    // Code signing only needed on macOS; zero-fill on other platforms.
    std::memset(out, 0, 32);
    (void)data;
    (void)len;
#endif
}

uint8_t pageSizeLog2(size_t pageSize) {
    uint8_t log2 = 0;
    size_t value = (pageSize == 0) ? 1 : pageSize;
    while (value > 1) {
        value >>= 1;
        ++log2;
    }
    return log2;
}

uint32_t codeSlotCount(size_t codeLimit, size_t pageSize) {
    const size_t effectivePageSize = (pageSize == 0) ? 1 : pageSize;
    return static_cast<uint32_t>((codeLimit + effectivePageSize - 1) / effectivePageSize);
}

uint32_t codeDirectorySize(size_t codeLimit, const std::string &identifier, size_t pageSize) {
    const uint32_t identLen = static_cast<uint32_t>(identifier.size() + 1);
    const uint32_t nCodeSlots = codeSlotCount(codeLimit, pageSize);
    const uint32_t hashOffset = kCodeDirectoryHeaderSize + identLen + kSpecialSlots * kHashSize;
    return hashOffset + nCodeSlots * kHashSize;
}

} // anonymous namespace

size_t estimateCodeSignatureSize(size_t codeLimit,
                                 const std::string &identifier,
                                 size_t pageSize) {
    return kSuperBlobHeaderSize + codeDirectorySize(codeLimit, identifier, pageSize) +
           kRequirementsSize + kBlobWrapperSize;
}

std::vector<uint8_t> buildCodeSignature(const std::vector<uint8_t> &file,
                                        size_t codeLimit,
                                        const std::string &identifier,
                                        uint64_t textSegFileOff,
                                        uint64_t textSegFileSize,
                                        size_t pageSize) {
    const uint32_t nCodeSlots = codeSlotCount(codeLimit, pageSize);
    const uint32_t identLen = static_cast<uint32_t>(identifier.size() + 1);
    const uint32_t hashOffset = kCodeDirectoryHeaderSize + identLen + kSpecialSlots * kHashSize;
    const uint32_t cdSize = codeDirectorySize(codeLimit, identifier, pageSize);
    const uint32_t codeLimit32 = static_cast<uint32_t>(
        std::min<size_t>(codeLimit, static_cast<size_t>(std::numeric_limits<uint32_t>::max())));
    const uint64_t codeLimit64 =
        (codeLimit > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
            ? static_cast<uint64_t>(codeLimit)
            : 0;

    // --- Build CodeDirectory ---
    std::vector<uint8_t> cd;
    cd.reserve(cdSize);
    writeBE32(cd, CSMAGIC_CODEDIRECTORY);
    writeBE32(cd, cdSize);
    writeBE32(cd, CD_VERSION);
    writeBE32(cd, CS_ADHOC);
    writeBE32(cd, hashOffset);   // hashOffset
    writeBE32(cd, kCodeDirectoryHeaderSize); // identOffset
    writeBE32(cd, kSpecialSlots);
    writeBE32(cd, nCodeSlots);
    writeBE32(cd, codeLimit32);
    cd.push_back(kHashSize);               // hashSize (SHA-256)
    cd.push_back(2);                       // hashType (SHA-256)
    cd.push_back(0);                       // platform
    cd.push_back(pageSizeLog2(pageSize));  // pageSize (log2 target page size)
    writeBE32(cd, 0);                      // spare2
    writeBE32(cd, 0);                      // scatterOffset (v >= 0x20100)
    writeBE32(cd, 0);                      // teamOffset (v >= 0x20200)
    writeBE32(cd, 0);                      // spare3 (v >= 0x20300)
    writeBE64(cd, codeLimit64);            // codeLimit64
    writeBE64(cd, textSegFileOff);         // execSegBase (v >= 0x20400)
    writeBE64(cd, textSegFileSize);        // execSegLimit
    writeBE64(cd, CS_EXECSEG_MAIN_BINARY); // execSegFlags

    // Identifier string.
    cd.insert(cd.end(), identifier.begin(), identifier.end());
    cd.push_back(0);

    // Special slot -2: requirements hash.
    std::vector<uint8_t> req;
    req.reserve(kRequirementsSize);
    writeBE32(req, CSMAGIC_REQUIREMENTS);
    writeBE32(req, kRequirementsSize);
    writeBE32(req, 0); // count = 0
    uint8_t reqHash[kHashSize];
    sha256(req.data(), req.size(), reqHash);
    cd.insert(cd.end(), reqHash, reqHash + kHashSize);

    // Special slot -1: no Info.plist hash for raw executables.
    cd.insert(cd.end(), kHashSize, 0);

    // Code slot hashes: SHA-256 of each page.
    for (uint32_t i = 0; i < nCodeSlots; ++i) {
        const size_t pageStart = static_cast<size_t>(i) * pageSize;
        const size_t pageEnd = std::min(pageStart + pageSize, codeLimit);
        uint8_t hash[32];
        sha256(file.data() + pageStart, pageEnd - pageStart, hash);
        cd.insert(cd.end(), hash, hash + 32);
    }

    // --- Build empty BlobWrapper slot ---
    std::vector<uint8_t> wrapper;
    wrapper.reserve(kBlobWrapperSize);
    writeBE32(wrapper, CSMAGIC_BLOBWRAPPER);
    writeBE32(wrapper, kBlobWrapperSize);

    // --- Build SuperBlob ---
    const uint32_t sbSize = kSuperBlobHeaderSize +
                            static_cast<uint32_t>(cd.size() + req.size() + wrapper.size());

    std::vector<uint8_t> sb;
    sb.reserve(sbSize);
    writeBE32(sb, CSMAGIC_EMBEDDED_SIGNATURE);
    writeBE32(sb, sbSize);
    writeBE32(sb, 3); // count = 3 blobs

    // Index entry 0: CodeDirectory (type = 0)
    writeBE32(sb, CSSLOT_CODEDIRECTORY);
    writeBE32(sb, kSuperBlobHeaderSize);

    // Index entry 1: Requirements (type = 2)
    writeBE32(sb, CSSLOT_REQUIREMENTS);
    writeBE32(sb, kSuperBlobHeaderSize + static_cast<uint32_t>(cd.size()));

    // Index entry 2: empty BlobWrapper (type = 0x10000)
    writeBE32(sb, CSSLOT_SIGNATURESLOT);
    writeBE32(
        sb,
        kSuperBlobHeaderSize + static_cast<uint32_t>(cd.size() + req.size()));

    sb.insert(sb.end(), cd.begin(), cd.end());
    sb.insert(sb.end(), req.begin(), req.end());
    sb.insert(sb.end(), wrapper.begin(), wrapper.end());
    return sb;
}

} // namespace viper::codegen::linker
