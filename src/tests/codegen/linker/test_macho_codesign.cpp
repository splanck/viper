//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/linker/test_macho_codesign.cpp
// Purpose: Verify Mach-O ad-hoc signature blobs match the Apple arm64 layout.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/MachOCodeSign.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#endif

using namespace viper::codegen::linker;

static int gFail = 0;

static void check(bool cond, const char *msg, int line) {
    if (!cond) {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

static uint32_t readBE32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

static uint64_t readBE64(const uint8_t *p) {
    return (static_cast<uint64_t>(readBE32(p)) << 32) | static_cast<uint64_t>(readBE32(p + 4));
}

static void sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
#if defined(__APPLE__)
    CC_SHA256(data, static_cast<CC_LONG>(len), out);
#else
    std::memset(out, 0, 32);
    (void)data;
    (void)len;
#endif
}

static void testAppleStyleSignatureLayout() {
    static constexpr uint32_t CSMAGIC_EMBEDDED_SIGNATURE = 0xFADE0CC0;
    static constexpr uint32_t CSMAGIC_CODEDIRECTORY = 0xFADE0C02;
    static constexpr uint32_t CSMAGIC_REQUIREMENTS = 0xFADE0C01;
    static constexpr uint32_t CSMAGIC_BLOBWRAPPER = 0xFADE0B01;
    static constexpr uint32_t CSSLOT_CODEDIRECTORY = 0;
    static constexpr uint32_t CSSLOT_REQUIREMENTS = 2;
    static constexpr uint32_t CSSLOT_SIGNATURESLOT = 0x10000;
    static constexpr uint32_t CS_ADHOC = 0x2;
    static constexpr uint32_t CD_VERSION = 0x20400;

    const size_t pageSize = 16384;
    const size_t codeLimit = 20000;
    const std::string identifier = "chess-zia";

    std::vector<uint8_t> file(codeLimit);
    for (size_t i = 0; i < file.size(); ++i)
        file[i] = static_cast<uint8_t>((i * 37U + 11U) & 0xFFU);

    const std::vector<uint8_t> sig =
        buildCodeSignature(file, codeLimit, identifier, 0, 0x8000, pageSize);
    CHECK(sig.size() == estimateCodeSignatureSize(codeLimit, identifier, pageSize));

    CHECK(sig.size() >= 36);
    CHECK(readBE32(sig.data()) == CSMAGIC_EMBEDDED_SIGNATURE);
    CHECK(readBE32(sig.data() + 4) == sig.size());
    CHECK(readBE32(sig.data() + 8) == 3);

    const uint32_t entry0Type = readBE32(sig.data() + 12);
    const uint32_t entry0Off = readBE32(sig.data() + 16);
    const uint32_t entry1Type = readBE32(sig.data() + 20);
    const uint32_t entry1Off = readBE32(sig.data() + 24);
    const uint32_t entry2Type = readBE32(sig.data() + 28);
    const uint32_t entry2Off = readBE32(sig.data() + 32);

    CHECK(entry0Type == CSSLOT_CODEDIRECTORY);
    CHECK(entry1Type == CSSLOT_REQUIREMENTS);
    CHECK(entry2Type == CSSLOT_SIGNATURESLOT);
    CHECK(entry0Off == 36);
    CHECK(entry0Off < entry1Off);
    CHECK(entry1Off < entry2Off);

    const uint8_t *cd = sig.data() + entry0Off;
    const uint8_t *req = sig.data() + entry1Off;
    const uint8_t *wrapper = sig.data() + entry2Off;

    CHECK(readBE32(cd + 0) == CSMAGIC_CODEDIRECTORY);
    CHECK(readBE32(cd + 8) == CD_VERSION);
    CHECK(readBE32(cd + 12) == CS_ADHOC);
    CHECK(readBE32(cd + 24) == 2);
    CHECK(readBE32(cd + 28) == 2);
    CHECK(readBE32(cd + 32) == codeLimit);
    CHECK(cd[36] == 32);
    CHECK(cd[37] == 2);
    CHECK(cd[39] == 14);
    CHECK(readBE64(cd + 56) == 0);
    CHECK(readBE64(cd + 64) == 0);
    CHECK(readBE64(cd + 72) == 0x8000);
    CHECK(readBE64(cd + 80) == 1);

    const uint32_t hashOffset = readBE32(cd + 16);
    const uint32_t identOffset = readBE32(cd + 20);
    const uint32_t cdLen = readBE32(cd + 4);
    CHECK(cdLen == sig.size() - 36 - 12 - 8);
    CHECK(identOffset == 88);
    CHECK(hashOffset == 88 + identifier.size() + 1 + 2 * 32);

    const char *identPtr = reinterpret_cast<const char *>(cd + identOffset);
    CHECK(std::string(identPtr) == identifier);

    CHECK(readBE32(req + 0) == CSMAGIC_REQUIREMENTS);
    CHECK(readBE32(req + 4) == 12);
    CHECK(readBE32(req + 8) == 0);

    CHECK(readBE32(wrapper + 0) == CSMAGIC_BLOBWRAPPER);
    CHECK(readBE32(wrapper + 4) == 8);

    const uint8_t *requirementsHash = cd + hashOffset - 64;
    const uint8_t *infoHash = cd + hashOffset - 32;
    const uint8_t *codeHash0 = cd + hashOffset;
    const uint8_t *codeHash1 = cd + hashOffset + 32;

    uint8_t expectedReqHash[32];
    sha256(req, 12, expectedReqHash);
    CHECK(std::memcmp(requirementsHash, expectedReqHash, 32) == 0);

    const uint8_t zeroHash[32] = {};
    CHECK(std::memcmp(infoHash, zeroHash, 32) == 0);

    uint8_t expectedCodeHash0[32];
    uint8_t expectedCodeHash1[32];
    sha256(file.data(), pageSize, expectedCodeHash0);
    sha256(file.data() + pageSize, codeLimit - pageSize, expectedCodeHash1);
    CHECK(std::memcmp(codeHash0, expectedCodeHash0, 32) == 0);
    CHECK(std::memcmp(codeHash1, expectedCodeHash1, 32) == 0);
}

int main() {
    testAppleStyleSignatureLayout();
    if (gFail != 0) {
        std::cerr << gFail << " test(s) failed\n";
        return 1;
    }
    return 0;
}
