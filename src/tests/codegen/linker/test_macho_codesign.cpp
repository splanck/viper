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

#include "codegen/common/linker/MachOBindRebase.hpp"
#include "codegen/common/linker/MachOCodeSign.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

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

// Portable SHA-256 (FIPS 180-4) so the expected hashes match the signer's
// dependency-free digest on every host, not just Apple platforms.
static void sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
    auto ror32 = [](uint32_t v, unsigned b) { return (v >> b) | (v << (32 - b)); };
    auto rd = [](const uint8_t *p) {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
    };
    static constexpr std::array<uint32_t, 64> k = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
        0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
        0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
        0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
        0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
        0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
        0xc67178f2u};
    uint32_t h[8] = {0x6a09e667u,
                     0xbb67ae85u,
                     0x3c6ef372u,
                     0xa54ff53au,
                     0x510e527fu,
                     0x9b05688cu,
                     0x1f83d9abu,
                     0x5be0cd19u};
    auto block = [&](const uint8_t *b) {
        uint32_t w[64] = {};
        for (size_t i = 0; i < 16; ++i)
            w[i] = rd(b + i * 4);
        for (size_t i = 16; i < 64; ++i) {
            const uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (size_t i = 0; i < 64; ++i) {
            const uint32_t S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            const uint32_t S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
            const uint32_t maj = (a & bb) ^ (a & c) ^ (bb & c);
            const uint32_t t2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = bb;
            bb = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += bb;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    };
    size_t off = 0;
    while (off + 64 <= len) {
        block(data + off);
        off += 64;
    }
    std::vector<uint8_t> tail(data + off, data + len);
    tail.push_back(0x80);
    while ((tail.size() % 64) != 56)
        tail.push_back(0);
    const uint64_t bits = static_cast<uint64_t>(len) * 8ull;
    for (int s = 56; s >= 0; s -= 8)
        tail.push_back(static_cast<uint8_t>((bits >> s) & 0xffu));
    for (size_t p = 0; p < tail.size(); p += 64)
        block(tail.data() + p);
    for (size_t i = 0; i < 8; ++i) {
        out[i * 4 + 0] = static_cast<uint8_t>((h[i] >> 24) & 0xffu);
        out[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xffu);
        out[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xffu);
        out[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xffu);
    }
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
    const std::string identifier = "chess";

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

static uint64_t readULEB(const std::vector<uint8_t> &data, size_t &pos) {
    uint64_t value = 0;
    unsigned shift = 0;
    while (pos < data.size()) {
        uint8_t byte = data[pos++];
        value |= static_cast<uint64_t>(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0)
            return value;
        shift += 7;
    }
    return value;
}

static size_t countRebaseFixups(const std::vector<uint8_t> &data) {
    size_t pos = 0;
    size_t count = 0;
    uint64_t offset = 0;
    while (pos < data.size()) {
        uint8_t byte = data[pos++];
        uint8_t opcode = byte & 0xF0u;
        uint8_t imm = byte & 0x0Fu;
        switch (opcode) {
            case 0x00:
                return count;
            case 0x10:
                break;
            case 0x20:
                offset = readULEB(data, pos);
                break;
            case 0x30:
                offset += readULEB(data, pos);
                break;
            case 0x40:
                offset += readULEB(data, pos) * 8;
                break;
            case 0x50:
                count += imm;
                offset += static_cast<uint64_t>(imm) * 8;
                break;
            case 0x60: {
                const uint64_t n = readULEB(data, pos);
                count += static_cast<size_t>(n);
                offset += n * 8;
                break;
            }
            case 0x70: {
                const uint64_t n = readULEB(data, pos);
                const uint64_t skip = readULEB(data, pos);
                count += static_cast<size_t>(n);
                offset += n * (skip + 8);
                break;
            }
            case 0x80:
                ++count;
                offset += static_cast<uint64_t>(imm) * 8 + 8;
                break;
            default:
                return count;
        }
    }
    return count;
}

static void testDuplicateRebaseEntriesAreCoalesced() {
    LinkLayout layout;
    OutputSection data;
    data.name = "__DATA,__objc_data";
    data.virtualAddr = 0x100010000;
    data.writable = true;
    data.data.resize(32);
    layout.sections.push_back(std::move(data));
    layout.rebaseEntries.push_back({0, 0});
    layout.rebaseEntries.push_back({0, 0});
    layout.rebaseEntries.push_back({0, 8});
    layout.rebaseEntries.push_back({0, 8});

    std::vector<uint8_t> rebaseData;
    buildRebaseOpcodes(rebaseData, layout, 0x100010000, 2);

    CHECK(countRebaseFixups(rebaseData) == 2);
}

// F4: known-answer test pinning the portable SHA-256 to FIPS 180-4 vectors, so
// the signer's digest is verified correct on every build host (not just Apple).
static void testSha256KnownAnswers() {
    auto hex = [](const uint8_t *h) {
        static const char *d = "0123456789abcdef";
        std::string s;
        for (int i = 0; i < 32; ++i) {
            s.push_back(d[h[i] >> 4]);
            s.push_back(d[h[i] & 0xf]);
        }
        return s;
    };
    uint8_t out[32];
    sha256(reinterpret_cast<const uint8_t *>(""), 0, out);
    CHECK(hex(out) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::string abc = "abc";
    sha256(reinterpret_cast<const uint8_t *>(abc.data()), abc.size(), out);
    CHECK(hex(out) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // A 1000-byte input crosses multiple 64-byte blocks and a padding block.
    const std::string big(1000, 'a');
    sha256(reinterpret_cast<const uint8_t *>(big.data()), big.size(), out);
    CHECK(hex(out) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

int main() {
    testSha256KnownAnswers();
    testAppleStyleSignatureLayout();
    testDuplicateRebaseEntriesAreCoalesced();
    if (gFail != 0) {
        std::cerr << gFail << " test(s) failed\n";
        return 1;
    }
    return 0;
}
