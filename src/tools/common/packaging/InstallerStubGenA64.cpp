//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/InstallerStubGenA64.cpp
// Purpose: AArch64 instruction encoding for native Windows ARM64 installer stubs.
//
// Key invariants:
//   - Instructions are emitted as little-endian 32-bit words.
//   - Branch fixups validate 4-byte alignment and signed 26-bit reach.
//   - Embedded data is kept separate from executable code.
//
// Ownership/Lifetime:
//   - Single-use builder.
//
// Links: InstallerStubGenA64.hpp
//
//===----------------------------------------------------------------------===//

#include "InstallerStubGenA64.hpp"
#include "PkgUtils.hpp"

#include <stdexcept>

namespace viper::pkg {

void InstallerStubGenA64::emit32(uint32_t word) {
    code_.push_back(static_cast<uint8_t>(word & 0xFF));
    code_.push_back(static_cast<uint8_t>((word >> 8) & 0xFF));
    code_.push_back(static_cast<uint8_t>((word >> 16) & 0xFF));
    code_.push_back(static_cast<uint8_t>((word >> 24) & 0xFF));
}

void InstallerStubGenA64::emitAddSubImm(uint32_t baseOpcode, A64Reg dst, A64Reg src, uint32_t imm) {
    uint32_t shift = 0;
    uint32_t imm12 = imm;
    if (imm12 > 0xFFFu) {
        if ((imm & 0xFFFu) != 0)
            throw std::runtime_error("InstallerStubGenA64: immediate is not encodable");
        shift = 1;
        imm12 = imm >> 12;
    }
    if (imm12 > 0xFFFu)
        throw std::runtime_error("InstallerStubGenA64: immediate is too large");
    emit32(baseOpcode | (shift << 22) | (imm12 << 10) | (static_cast<uint32_t>(regBits(src)) << 5) |
           regBits(dst));
}

void InstallerStubGenA64::emitLoadStore64(uint32_t baseOpcode,
                                          A64Reg reg,
                                          A64Reg base,
                                          uint32_t offset) {
    if ((offset & 7u) != 0 || offset > 0x7FFFu)
        throw std::runtime_error("InstallerStubGenA64: 64-bit memory offset is not encodable");
    emit32(baseOpcode | ((offset / 8u) << 10) | (static_cast<uint32_t>(regBits(base)) << 5) |
           regBits(reg));
}

void InstallerStubGenA64::emitLoadStore32(uint32_t baseOpcode,
                                          A64Reg reg,
                                          A64Reg base,
                                          uint32_t offset) {
    if ((offset & 3u) != 0 || offset > 0x3FFFu)
        throw std::runtime_error("InstallerStubGenA64: 32-bit memory offset is not encodable");
    emit32(baseOpcode | ((offset / 4u) << 10) | (static_cast<uint32_t>(regBits(base)) << 5) |
           regBits(reg));
}

void InstallerStubGenA64::emitAdrPair(A64Reg dst,
                                      uint32_t target,
                                      FixupKind pageKind,
                                      FixupKind lowKind) {
    fixups_.push_back({static_cast<uint32_t>(code_.size()), target, pageKind});
    emit32(0x90000000u | regBits(dst)); // adrp dst, target_page
    fixups_.push_back({static_cast<uint32_t>(code_.size()), target, lowKind});
    emit32(0x91000000u | (static_cast<uint32_t>(regBits(dst)) << 5) | regBits(dst));
}

uint32_t InstallerStubGenA64::newLabel() {
    const uint32_t id = static_cast<uint32_t>(labels_.size());
    labels_.push_back({});
    return id;
}

void InstallerStubGenA64::bindLabel(uint32_t labelId) {
    if (labelId >= labels_.size())
        throw std::runtime_error("InstallerStubGenA64: invalid label ID");
    labels_[labelId].codeOffset = static_cast<uint32_t>(code_.size());
    labels_[labelId].bound = true;
}

void InstallerStubGenA64::ret() {
    emit32(0xD65F03C0u);
}

void InstallerStubGenA64::nop() {
    emit32(0xD503201Fu);
}

void InstallerStubGenA64::movRegImm32(A64Reg dst, uint32_t imm) {
    emit32(0xD2800000u | ((imm & 0xFFFFu) << 5) | regBits(dst));
    const uint32_t hi = (imm >> 16) & 0xFFFFu;
    if (hi != 0)
        emit32(0xF2A00000u | (hi << 5) | regBits(dst));
}

void InstallerStubGenA64::movRegReg(A64Reg dst, A64Reg src) {
    if (dst == A64Reg::SP || src == A64Reg::SP) {
        addRegImm(dst, src, 0);
        return;
    }
    emit32(0xAA0003E0u | (static_cast<uint32_t>(regBits(src)) << 16) | regBits(dst));
}

void InstallerStubGenA64::addRegImm(A64Reg dst, A64Reg src, uint32_t imm) {
    emitAddSubImm(0x91000000u, dst, src, imm);
}

void InstallerStubGenA64::subRegImm(A64Reg dst, A64Reg src, uint32_t imm) {
    emitAddSubImm(0xD1000000u, dst, src, imm);
}

void InstallerStubGenA64::loadMem64(A64Reg dst, A64Reg base, uint32_t offset) {
    emitLoadStore64(0xF9400000u, dst, base, offset);
}

void InstallerStubGenA64::storeMem64(A64Reg base, uint32_t offset, A64Reg src) {
    emitLoadStore64(0xF9000000u, src, base, offset);
}

void InstallerStubGenA64::storeMem32(A64Reg base, uint32_t offset, A64Reg src) {
    emitLoadStore32(0xB9000000u, src, base, offset);
}

void InstallerStubGenA64::storeMemImm32(A64Reg base, uint32_t offset, uint32_t imm) {
    movRegImm32(A64Reg::X17, imm);
    storeMem32(base, offset, A64Reg::X17);
}

void InstallerStubGenA64::leaData(A64Reg dst, uint32_t dataOffset) {
    emitAdrPair(dst, dataOffset, FixupKind::DataPage21, FixupKind::DataLow12);
}

void InstallerStubGenA64::b(uint32_t labelId) {
    fixups_.push_back({static_cast<uint32_t>(code_.size()), labelId, FixupKind::Branch26});
    emit32(0x14000000u);
}

void InstallerStubGenA64::cbz(A64Reg reg, uint32_t labelId) {
    fixups_.push_back({static_cast<uint32_t>(code_.size()), labelId, FixupKind::CompareBranch19});
    emit32(0xB4000000u | regBits(reg));
}

void InstallerStubGenA64::blr(A64Reg target) {
    emit32(0xD63F0000u | (static_cast<uint32_t>(regBits(target)) << 5));
}

void InstallerStubGenA64::callIATSlot(uint32_t flatIndex) {
    emitAdrPair(A64Reg::X16, flatIndex, FixupKind::IATPage21, FixupKind::IATLow12);
    loadMem64(A64Reg::X16, A64Reg::X16, 0);
    blr(A64Reg::X16);
}

uint32_t InstallerStubGenA64::embedStringW(const std::string &asciiStr) {
    const uint32_t offset = static_cast<uint32_t>(data_.size());
    const auto bytes = utf8ToUtf16LEBytes(asciiStr, true);
    data_.insert(data_.end(), bytes.begin(), bytes.end());
    return offset;
}

uint32_t InstallerStubGenA64::embedBytes(const void *data, size_t len) {
    const uint32_t offset = static_cast<uint32_t>(data_.size());
    const auto *bytes = static_cast<const uint8_t *>(data);
    data_.insert(data_.end(), bytes, bytes + len);
    return offset;
}

static void patchA64LE32(std::vector<uint8_t> &buf, uint32_t off, uint32_t word) {
    buf[off + 0] = static_cast<uint8_t>(word & 0xFF);
    buf[off + 1] = static_cast<uint8_t>((word >> 8) & 0xFF);
    buf[off + 2] = static_cast<uint8_t>((word >> 16) & 0xFF);
    buf[off + 3] = static_cast<uint8_t>((word >> 24) & 0xFF);
}

static uint32_t flatIATOffsetA64(const std::vector<PEImport> &imports, uint32_t flatIndex) {
    uint32_t current = 0;
    uint32_t slot = 0;
    for (const auto &imp : imports) {
        for (size_t i = 0; i < imp.functions.size(); ++i, ++slot) {
            if (slot == flatIndex)
                return current + static_cast<uint32_t>(i * 8u);
        }
        current += static_cast<uint32_t>((imp.functions.size() + 1u) * 8u);
    }
    throw std::runtime_error("InstallerStubGenA64: IAT slot index out of range");
}

static void patchA64Adrp(std::vector<uint8_t> &buf,
                         uint32_t off,
                         uint32_t existing,
                         uint32_t textRVA,
                         uint32_t targetRVA) {
    const int64_t pcPage = static_cast<int64_t>((textRVA + off) & static_cast<uint32_t>(~0xFFFu));
    const int64_t targetPage = static_cast<int64_t>(targetRVA & static_cast<uint32_t>(~0xFFFu));
    const int64_t pageDelta = (targetPage - pcPage) / 4096;
    if (pageDelta < -(1ll << 20) || pageDelta >= (1ll << 20))
        throw std::runtime_error("InstallerStubGenA64: ADRP target is out of range");
    const uint32_t imm = static_cast<uint32_t>(pageDelta) & 0x1FFFFFu;
    const uint32_t encoded =
        (existing & 0x9F00001Fu) | ((imm & 0x3u) << 29) | (((imm >> 2) & 0x7FFFFu) << 5);
    patchA64LE32(buf, off, encoded);
}

static void patchA64AddLow12(std::vector<uint8_t> &buf, uint32_t off, uint32_t targetRVA) {
    const uint32_t existing =
        static_cast<uint32_t>(buf[off]) | (static_cast<uint32_t>(buf[off + 1]) << 8) |
        (static_cast<uint32_t>(buf[off + 2]) << 16) | (static_cast<uint32_t>(buf[off + 3]) << 24);
    const uint32_t encoded = (existing & ~(0xFFFu << 10)) | ((targetRVA & 0xFFFu) << 10);
    patchA64LE32(buf, off, encoded);
}

std::vector<uint8_t> InstallerStubGenA64::finishText(uint32_t textRVA,
                                                     uint32_t iatBaseRVA,
                                                     const std::vector<PEImport> &imports,
                                                     uint32_t dataBaseRVA) const {
    std::vector<uint8_t> result = code_;
    for (const auto &fixup : fixups_) {
        switch (fixup.kind) {
            case FixupKind::Branch26: {
                if (fixup.target >= labels_.size() || !labels_[fixup.target].bound)
                    throw std::runtime_error("InstallerStubGenA64: unbound label in fixup");
                const int64_t delta = static_cast<int64_t>(labels_[fixup.target].codeOffset) -
                                      static_cast<int64_t>(fixup.codeOffset);
                if ((delta & 3) != 0)
                    throw std::runtime_error("InstallerStubGenA64: branch target is not aligned");
                const int64_t imm26 = delta / 4;
                if (imm26 < -(1ll << 25) || imm26 >= (1ll << 25))
                    throw std::runtime_error("InstallerStubGenA64: branch target is out of range");
                const uint32_t encoded = 0x14000000u | (static_cast<uint32_t>(imm26) & 0x03FFFFFFu);
                patchA64LE32(result, fixup.codeOffset, encoded);
                break;
            }
            case FixupKind::CompareBranch19: {
                if (fixup.target >= labels_.size() || !labels_[fixup.target].bound)
                    throw std::runtime_error("InstallerStubGenA64: unbound label in fixup");
                const int64_t delta = static_cast<int64_t>(labels_[fixup.target].codeOffset) -
                                      static_cast<int64_t>(fixup.codeOffset);
                if ((delta & 3) != 0)
                    throw std::runtime_error("InstallerStubGenA64: branch target is not aligned");
                const int64_t imm19 = delta / 4;
                if (imm19 < -(1ll << 18) || imm19 >= (1ll << 18))
                    throw std::runtime_error("InstallerStubGenA64: CBZ target is out of range");
                const uint32_t existing =
                    static_cast<uint32_t>(result[fixup.codeOffset]) |
                    (static_cast<uint32_t>(result[fixup.codeOffset + 1]) << 8) |
                    (static_cast<uint32_t>(result[fixup.codeOffset + 2]) << 16) |
                    (static_cast<uint32_t>(result[fixup.codeOffset + 3]) << 24);
                const uint32_t encoded =
                    (existing & 0xFF00001Fu) | ((static_cast<uint32_t>(imm19) & 0x7FFFFu) << 5);
                patchA64LE32(result, fixup.codeOffset, encoded);
                break;
            }
            case FixupKind::IATPage21:
            case FixupKind::IATLow12: {
                const uint32_t targetRVA = iatBaseRVA + flatIATOffsetA64(imports, fixup.target);
                if (fixup.kind == FixupKind::IATPage21) {
                    const uint32_t existing =
                        static_cast<uint32_t>(result[fixup.codeOffset]) |
                        (static_cast<uint32_t>(result[fixup.codeOffset + 1]) << 8) |
                        (static_cast<uint32_t>(result[fixup.codeOffset + 2]) << 16) |
                        (static_cast<uint32_t>(result[fixup.codeOffset + 3]) << 24);
                    patchA64Adrp(result, fixup.codeOffset, existing, textRVA, targetRVA);
                } else {
                    patchA64AddLow12(result, fixup.codeOffset, targetRVA);
                }
                break;
            }
            case FixupKind::DataPage21:
            case FixupKind::DataLow12: {
                const uint32_t targetRVA = dataBaseRVA + fixup.target;
                if (fixup.kind == FixupKind::DataPage21) {
                    const uint32_t existing =
                        static_cast<uint32_t>(result[fixup.codeOffset]) |
                        (static_cast<uint32_t>(result[fixup.codeOffset + 1]) << 8) |
                        (static_cast<uint32_t>(result[fixup.codeOffset + 2]) << 16) |
                        (static_cast<uint32_t>(result[fixup.codeOffset + 3]) << 24);
                    patchA64Adrp(result, fixup.codeOffset, existing, textRVA, targetRVA);
                } else {
                    patchA64AddLow12(result, fixup.codeOffset, targetRVA);
                }
                break;
            }
            default:
                throw std::runtime_error("InstallerStubGenA64: unknown fixup kind");
        }
    }
    return result;
}

} // namespace viper::pkg
