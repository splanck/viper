//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/InstallerStubGenA64.hpp
// Purpose: Minimal AArch64 instruction emitter for native Windows ARM64
//          installer bootstrap code.
//
// Key invariants:
//   - Emits fixed-width 32-bit AArch64 instructions in little-endian order.
//   - Labels support forward references resolved during finishText().
//   - Embedded string/data bytes live in a separate .rdata buffer.
//
// Ownership/Lifetime:
//   - Single-use builder. Call finishText() once after all labels are bound.
//
// Links: InstallerStub.hpp, InstallerStubGen.hpp, PEBuilder.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "PEBuilder.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zanna::pkg {

/// @brief AArch64 general-purpose register identifiers.
enum class A64Reg : uint8_t {
    X0 = 0,
    X1 = 1,
    X2 = 2,
    X3 = 3,
    X4 = 4,
    X5 = 5,
    X6 = 6,
    X7 = 7,
    X8 = 8,
    X9 = 9,
    X10 = 10,
    X11 = 11,
    X12 = 12,
    X13 = 13,
    X14 = 14,
    X15 = 15,
    X16 = 16,
    X17 = 17,
    X18 = 18,
    X19 = 19,
    X20 = 20,
    X21 = 21,
    X22 = 22,
    X23 = 23,
    X24 = 24,
    X25 = 25,
    X26 = 26,
    X27 = 27,
    X28 = 28,
    X29 = 29,
    X30 = 30,
    SP = 31,
};

/// @brief Minimal AArch64 emitter used by native Windows ARM64 installer stubs.
class InstallerStubGenA64 {
  public:
    uint32_t newLabel();
    void bindLabel(uint32_t labelId);

    void ret();
    void nop();
    void movRegImm32(A64Reg dst, uint32_t imm);
    void movRegReg(A64Reg dst, A64Reg src);
    void addRegImm(A64Reg dst, A64Reg src, uint32_t imm);
    void subRegImm(A64Reg dst, A64Reg src, uint32_t imm);
    void loadMem64(A64Reg dst, A64Reg base, uint32_t offset);
    void storeMem64(A64Reg base, uint32_t offset, A64Reg src);
    void storeMem32(A64Reg base, uint32_t offset, A64Reg src);
    void storeMemImm32(A64Reg base, uint32_t offset, uint32_t imm);
    void leaData(A64Reg dst, uint32_t dataOffset);
    void b(uint32_t labelId);
    void cbz(A64Reg reg, uint32_t labelId);
    void blr(A64Reg target);
    void callIATSlot(uint32_t flatIndex);

    uint32_t embedStringW(const std::string &asciiStr);
    uint32_t embedBytes(const void *data, size_t len);

    std::vector<uint8_t> finishText(uint32_t textRVA,
                                    uint32_t iatBaseRVA,
                                    const std::vector<PEImport> &imports,
                                    uint32_t dataBaseRVA) const;

    const std::vector<uint8_t> &dataSection() const {
        return data_;
    }

    size_t codeSize() const {
        return code_.size();
    }

  private:
    enum class FixupKind : uint8_t {
        Branch26,
        CompareBranch19,
        IATPage21,
        IATLow12,
        DataPage21,
        DataLow12,
    };

    struct LabelInfo {
        uint32_t codeOffset{0};
        bool bound{false};
    };

    struct Fixup {
        uint32_t codeOffset{0};
        uint32_t target{0};
        FixupKind kind{FixupKind::Branch26};
    };

    std::vector<uint8_t> code_;
    std::vector<uint8_t> data_;
    std::vector<LabelInfo> labels_;
    std::vector<Fixup> fixups_;

    void emit32(uint32_t word);
    void emitAddSubImm(uint32_t baseOpcode, A64Reg dst, A64Reg src, uint32_t imm);
    void emitAdrPair(A64Reg dst, uint32_t target, FixupKind pageKind, FixupKind lowKind);
    void emitLoadStore64(uint32_t baseOpcode, A64Reg reg, A64Reg base, uint32_t offset);
    void emitLoadStore32(uint32_t baseOpcode, A64Reg reg, A64Reg base, uint32_t offset);

    uint8_t regBits(A64Reg reg) const {
        return static_cast<uint8_t>(reg) & 31u;
    }
};

} // namespace zanna::pkg
