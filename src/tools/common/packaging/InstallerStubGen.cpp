//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/InstallerStubGen.cpp
// Purpose: x86-64 instruction encoding for installer stub generation.
//
// Key invariants:
//   - All encodings follow Intel x86-64 reference (Volume 2).
//   - REX prefix emitted when using R8-R15 or 64-bit operand size.
//   - Label fixups resolved in finishText() — forward jumps use placeholder 0.
//   - IAT calls use RIP-relative addressing: ff 15 [disp32].
//
// Ownership/Lifetime:
//   - Single-use builder.
//
// Links: InstallerStubGen.hpp
//
//===----------------------------------------------------------------------===//

#include "InstallerStubGen.hpp"

#include <cstring>
#include <stdexcept>

namespace viper::pkg {

// ============================================================================
// Helpers
// ============================================================================

void InstallerStubGen::emit(uint8_t b) {
    code_.push_back(b);
}

void InstallerStubGen::emit32(uint32_t v) {
    code_.push_back(static_cast<uint8_t>(v & 0xFF));
    code_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    code_.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    code_.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void InstallerStubGen::emitREX(bool w, X64Reg reg, X64Reg rm) {
    uint8_t rex = 0x40;
    if (w)
        rex |= 0x08; // REX.W
    if (needsREX_B(reg))
        rex |= 0x04; // REX.R (extends ModRM reg field)
    if (needsREX_B(rm))
        rex |= 0x01; // REX.B (extends ModRM r/m field)
    // Always emit REX if W bit needed or if extended registers used
    if (rex != 0x40 || w)
        emit(rex);
}

void InstallerStubGen::emitModRM(uint8_t mod, uint8_t reg, uint8_t rm) {
    emit(static_cast<uint8_t>((mod << 6) | (reg << 3) | rm));
}

void InstallerStubGen::emitModRMDisp32(uint8_t reg, X64Reg base, int32_t disp) {
    uint8_t baseBits = regBits(base);
    if (baseBits == 4) {
        // RSP/R12 needs SIB byte
        emitModRM(2, reg, 4); // mod=10, rm=100 (SIB follows)
        emit(0x24);           // SIB: scale=0, index=RSP(none), base=RSP
    } else if (baseBits == 5 && disp == 0) {
        // RBP/R13 with disp=0 needs explicit disp8=0 (mod=01)
        emitModRM(1, reg, baseBits);
        emit(0);
        return;
    } else {
        emitModRM(2, reg, baseBits); // mod=10 (disp32)
    }
    emit32(static_cast<uint32_t>(disp));
}

// ============================================================================
// Label Management
// ============================================================================

uint32_t InstallerStubGen::newLabel() {
    uint32_t id = static_cast<uint32_t>(labels_.size());
    labels_.push_back({0, false});
    return id;
}

void InstallerStubGen::bindLabel(uint32_t labelId) {
    if (labelId >= labels_.size())
        throw std::runtime_error("InstallerStubGen: invalid label ID");
    labels_[labelId].codeOffset = static_cast<uint32_t>(code_.size());
    labels_[labelId].bound = true;
}

// ============================================================================
// Basic Instructions
// ============================================================================

void InstallerStubGen::push(X64Reg r) {
    if (needsREX_B(r))
        emit(0x41); // REX.B
    emit(static_cast<uint8_t>(0x50 + regBits(r)));
}

void InstallerStubGen::pop(X64Reg r) {
    if (needsREX_B(r))
        emit(0x41); // REX.B
    emit(static_cast<uint8_t>(0x58 + regBits(r)));
}

void InstallerStubGen::ret() {
    emit(0xC3);
}

void InstallerStubGen::nop() {
    emit(0x90);
}

// ============================================================================
// MOV
// ============================================================================

void InstallerStubGen::movRegReg(X64Reg dst, X64Reg src) {
    // REX.W + 89 /r (mov r/m64, r64 — src is reg field)
    emitREX(true, src, dst);
    emit(0x89);
    emitModRM(3, regBits(src), regBits(dst));
}

void InstallerStubGen::movRegImm32(X64Reg dst, uint32_t imm) {
    // For R8-R15, need REX prefix; use B8+rd with 32-bit imm (zero-extends)
    if (needsREX_B(dst))
        emit(0x41);
    emit(static_cast<uint8_t>(0xB8 + regBits(dst)));
    emit32(imm);
}

void InstallerStubGen::movRegImm64(X64Reg dst, uint64_t imm) {
    // REX.W + B8+rd + imm64
    uint8_t rex = 0x48;
    if (needsREX_B(dst))
        rex |= 0x01;
    emit(rex);
    emit(static_cast<uint8_t>(0xB8 + regBits(dst)));
    // Emit 64-bit immediate
    for (int i = 0; i < 8; i++)
        emit(static_cast<uint8_t>((imm >> (i * 8)) & 0xFF));
}

void InstallerStubGen::movRegMem(X64Reg dst, X64Reg base, int32_t disp) {
    // REX.W + 8B /r (mov r64, r/m64)
    emitREX(true, dst, base);
    emit(0x8B);
    emitModRMDisp32(regBits(dst), base, disp);
}

void InstallerStubGen::movMemReg(X64Reg base, int32_t disp, X64Reg src) {
    // REX.W + 89 /r (mov r/m64, r64)
    emitREX(true, src, base);
    emit(0x89);
    emitModRMDisp32(regBits(src), base, disp);
}

void InstallerStubGen::movMemImm32(X64Reg base, int32_t disp, uint32_t imm) {
    // C7 /0 id (mov r/m32, imm32) — no REX.W so it's 32-bit
    if (needsREX_B(base))
        emit(0x41);
    emit(0xC7);
    emitModRMDisp32(0, base, disp);
    emit32(imm);
}

// ============================================================================
// LEA
// ============================================================================

void InstallerStubGen::leaRegMem(X64Reg dst, X64Reg base, int32_t disp) {
    // REX.W + 8D /r
    emitREX(true, dst, base);
    emit(0x8D);
    emitModRMDisp32(regBits(dst), base, disp);
}

// ============================================================================
// Arithmetic
// ============================================================================

void InstallerStubGen::subRegImm32(X64Reg dst, uint32_t imm) {
    // REX.W + 81 /5 id
    emitREX(true, X64Reg::RAX, dst);
    emit(0x81);
    emitModRM(3, 5, regBits(dst));
    emit32(imm);
}

void InstallerStubGen::addRegImm32(X64Reg dst, uint32_t imm) {
    // REX.W + 81 /0 id
    emitREX(true, X64Reg::RAX, dst);
    emit(0x81);
    emitModRM(3, 0, regBits(dst));
    emit32(imm);
}

void InstallerStubGen::addRegReg(X64Reg dst, X64Reg src) {
    // REX.W + 01 /r (add r/m64, r64)
    emitREX(true, src, dst);
    emit(0x01);
    emitModRM(3, regBits(src), regBits(dst));
}

void InstallerStubGen::xorRegReg(X64Reg dst, X64Reg src) {
    // REX.W + 31 /r
    emitREX(true, src, dst);
    emit(0x31);
    emitModRM(3, regBits(src), regBits(dst));
}

// ============================================================================
// Compare / Test
// ============================================================================

void InstallerStubGen::testRegReg(X64Reg a, X64Reg b) {
    // REX.W + 85 /r
    emitREX(true, b, a);
    emit(0x85);
    emitModRM(3, regBits(b), regBits(a));
}

void InstallerStubGen::cmpRegImm32(X64Reg r, uint32_t imm) {
    // REX.W + 81 /7 id
    emitREX(true, X64Reg::RAX, r);
    emit(0x81);
    emitModRM(3, 7, regBits(r));
    emit32(imm);
}

void InstallerStubGen::cmpRegReg(X64Reg a, X64Reg b) {
    // REX.W + 39 /r
    emitREX(true, b, a);
    emit(0x39);
    emitModRM(3, regBits(b), regBits(a));
}

// ============================================================================
// Conditional Jumps
// ============================================================================

void InstallerStubGen::jz(uint32_t labelId) {
    // 0F 84 cd (jz rel32)
    emit(0x0F);
    emit(0x84);
    fixups_.push_back({static_cast<uint32_t>(code_.size()), labelId, FixupKind::Rel32});
    emit32(0); // placeholder
}

void InstallerStubGen::jnz(uint32_t labelId) {
    // 0F 85 cd (jnz rel32)
    emit(0x0F);
    emit(0x85);
    fixups_.push_back({static_cast<uint32_t>(code_.size()), labelId, FixupKind::Rel32});
    emit32(0); // placeholder
}

void InstallerStubGen::jmp(uint32_t labelId) {
    // E9 cd (jmp rel32)
    emit(0xE9);
    fixups_.push_back({static_cast<uint32_t>(code_.size()), labelId, FixupKind::Rel32});
    emit32(0); // placeholder
}

// ============================================================================
// Call
// ============================================================================

void InstallerStubGen::callIATSlot(uint32_t flatIndex) {
    // FF 15 [disp32] — call [rip + disp32]
    emit(0xFF);
    emit(0x15);
    fixups_.push_back({static_cast<uint32_t>(code_.size()), flatIndex, FixupKind::IATSlotIndex});
    emit32(0); // placeholder — resolved in finishText()
}

void InstallerStubGen::leaRipData(X64Reg dst, uint32_t dataOffset) {
    // REX.W + 8D /r with ModRM(00, reg, 101) = RIP-relative addressing
    emitREX(true, dst, X64Reg::RBP); // RBP's regBits = 5 = 101b (RIP-relative encoding)
    emit(0x8D);
    emitModRM(0, regBits(dst), 5); // mod=00, rm=101 = RIP+disp32
    fixups_.push_back({static_cast<uint32_t>(code_.size()), dataOffset, FixupKind::DataRel32});
    emit32(0); // placeholder — resolved in finishText()
}

// ============================================================================
// Data Embedding
// ============================================================================

uint32_t InstallerStubGen::embedStringW(const std::string &asciiStr) {
    uint32_t offset = static_cast<uint32_t>(data_.size());
    // Convert ASCII to UTF-16LE
    for (char c : asciiStr) {
        data_.push_back(static_cast<uint8_t>(c));
        data_.push_back(0);
    }
    // NUL terminator (2 bytes)
    data_.push_back(0);
    data_.push_back(0);
    return offset;
}

uint32_t InstallerStubGen::embedBytes(const void *data, size_t len) {
    uint32_t offset = static_cast<uint32_t>(data_.size());
    auto *p = static_cast<const uint8_t *>(data);
    data_.insert(data_.end(), p, p + len);
    return offset;
}

// ============================================================================
// Finalization
// ============================================================================

/// @brief Compute the IAT slot RVA for a given flat function index.
static uint32_t computeIATSlotRVA(const std::vector<PEImport> &imports,
                                  uint32_t iatBaseRVA,
                                  uint32_t flatIndex) {
    uint32_t offset = 0;
    uint32_t idx = 0;
    for (const auto &dll : imports) {
        for (size_t f = 0; f < dll.functions.size(); ++f) {
            if (idx == flatIndex)
                return iatBaseRVA + offset;
            offset += 8;
            idx++;
        }
        offset += 8; // null terminator after each DLL's entries
    }
    return iatBaseRVA + offset;
}

static void patchLE32(std::vector<uint8_t> &buf, uint32_t off, int32_t val) {
    buf[off + 0] = static_cast<uint8_t>(val & 0xFF);
    buf[off + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    buf[off + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    buf[off + 3] = static_cast<uint8_t>((val >> 24) & 0xFF);
}

std::vector<uint8_t> InstallerStubGen::finishText(uint32_t textRVA,
                                                  uint32_t iatBaseRVA,
                                                  const std::vector<PEImport> &imports,
                                                  uint32_t dataBaseRVA) const {
    std::vector<uint8_t> result = code_;

    for (const auto &f : fixups_) {
        // RIP at the end of the disp32 field = textRVA + f.codeOffset + 4
        uint32_t rip = textRVA + f.codeOffset + 4;

        switch (f.kind) {
            case FixupKind::Rel32: {
                // Resolve label-relative jump (code → code)
                if (f.target >= labels_.size() || !labels_[f.target].bound)
                    throw std::runtime_error("InstallerStubGen: unbound label in fixup");
                int32_t rel = static_cast<int32_t>(labels_[f.target].codeOffset) -
                              static_cast<int32_t>(f.codeOffset + 4);
                patchLE32(result, f.codeOffset, rel);
                break;
            }
            case FixupKind::IATSlotIndex: {
                // Resolve flat function index → IAT slot RVA → RIP-relative
                uint32_t slotRVA = computeIATSlotRVA(imports, iatBaseRVA, f.target);
                int32_t rel = static_cast<int32_t>(slotRVA) - static_cast<int32_t>(rip);
                patchLE32(result, f.codeOffset, rel);
                break;
            }
            case FixupKind::DataRel32: {
                // Resolve data offset → absolute RVA → RIP-relative
                uint32_t dataRVA = dataBaseRVA + f.target;
                int32_t rel = static_cast<int32_t>(dataRVA) - static_cast<int32_t>(rip);
                patchLE32(result, f.codeOffset, rel);
                break;
            }
        }
    }

    return result;
}

} // namespace viper::pkg
