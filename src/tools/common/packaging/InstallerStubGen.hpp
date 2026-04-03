//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/InstallerStubGen.hpp
// Purpose: Minimal x86-64 instruction emitter for generating Windows installer
//          and uninstaller stub machine code at packaging time.
//
// Key invariants:
//   - Emits Windows x64 ABI compatible code (shadow space, RCX/RDX/R8/R9).
//   - Labels support forward references with automatic fixup on finish().
//   - String data is embedded in a separate .rdata buffer.
//   - All generated code uses RIP-relative addressing for position independence.
//
// Ownership/Lifetime:
//   - Single-use builder. Call finish*() once to get output.
//
// Links: InstallerStub.hpp, PEBuilder.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "PEBuilder.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace viper::pkg {

/// @brief x86-64 register identifiers.
enum class X64Reg : uint8_t {
    RAX = 0,
    RCX = 1,
    RDX = 2,
    RBX = 3,
    RSP = 4,
    RBP = 5,
    RSI = 6,
    RDI = 7,
    R8 = 8,
    R9 = 9,
    R10 = 10,
    R11 = 11,
    R12 = 12,
    R13 = 13,
    R14 = 14,
    R15 = 15
};

/// @brief Minimal x86-64 instruction emitter for installer stubs.
///
/// Generates position-independent code that calls Win32 APIs through
/// IAT slots using RIP-relative `call [rip+disp32]` instructions.
class InstallerStubGen {
  public:
    // ─── Label Management ─────────────────────────────────────────────

    /// @brief Create a new unbound label. Returns label ID.
    uint32_t newLabel();

    /// @brief Bind a label to the current code position.
    void bindLabel(uint32_t labelId);

    // ─── Basic Instructions ───────────────────────────────────────────

    void push(X64Reg r);
    void pop(X64Reg r);
    void ret();
    void nop();

    // ─── MOV ──────────────────────────────────────────────────────────

    /// @brief mov dst, src (64-bit reg-to-reg)
    void movRegReg(X64Reg dst, X64Reg src);

    /// @brief mov dst, imm32 (zero-extended to 64-bit)
    void movRegImm32(X64Reg dst, uint32_t imm);

    /// @brief mov dst, imm64 (full 64-bit immediate)
    void movRegImm64(X64Reg dst, uint64_t imm);

    /// @brief mov dst, [src + disp32]  (load 64-bit from memory)
    void movRegMem(X64Reg dst, X64Reg base, int32_t disp);

    /// @brief mov [dst + disp32], src  (store 64-bit to memory)
    void movMemReg(X64Reg base, int32_t disp, X64Reg src);

    /// @brief mov dword [dst + disp32], imm32 (store 32-bit immediate)
    void movMemImm32(X64Reg base, int32_t disp, uint32_t imm);

    // ─── LEA ──────────────────────────────────────────────────────────

    /// @brief lea dst, [base + disp32]
    void leaRegMem(X64Reg dst, X64Reg base, int32_t disp);

    // ─── Arithmetic ───────────────────────────────────────────────────

    void subRegImm32(X64Reg dst, uint32_t imm);
    void addRegImm32(X64Reg dst, uint32_t imm);
    void addRegReg(X64Reg dst, X64Reg src);
    void xorRegReg(X64Reg dst, X64Reg src);

    // ─── Compare / Test ───────────────────────────────────────────────

    void testRegReg(X64Reg a, X64Reg b);
    void cmpRegImm32(X64Reg r, uint32_t imm);
    void cmpRegReg(X64Reg a, X64Reg b);

    // ─── Conditional Jumps ────────────────────────────────────────────

    void jz(uint32_t labelId);
    void jnz(uint32_t labelId);
    void jmp(uint32_t labelId);

    // ─── Call ─────────────────────────────────────────────────────────

    /// @brief call [rip + disp32] — call through IAT slot by flat index.
    /// @param flatIndex  Flat function index across all DLLs in the import list.
    ///
    /// The actual IAT slot RVA is computed during finishText() from the
    /// import list and IAT base RVA.
    void callIATSlot(uint32_t flatIndex);

    // ─── Data Embedding ───────────────────────────────────────────────

    /// @brief lea dst, [rip + disp32] — load address of embedded data.
    /// @param dst        Destination register.
    /// @param dataOffset Offset within the data section (from embedStringW).
    void leaRipData(X64Reg dst, uint32_t dataOffset);

    // ─── Data Embedding ───────────────────────────────────────────────

    /// @brief Embed a UTF-16LE string in the data section.
    /// @return Offset within the data section.
    uint32_t embedStringW(const std::string &asciiStr);

    /// @brief Embed a raw byte array in the data section.
    /// @return Offset within the data section.
    uint32_t embedBytes(const void *data, size_t len);

    // ─── Output ───────────────────────────────────────────────────────

    /// @brief Finalize the code section, resolving all labels and fixups.
    /// @param textRVA        The RVA at which .text will be loaded.
    /// @param iatBaseRVA     The base RVA of the IAT within .rdata.
    /// @param imports        The import list (for IAT slot index resolution).
    /// @param dataBaseRVA    The base RVA where embedded data starts in .rdata.
    /// @return The completed .text section bytes.
    std::vector<uint8_t> finishText(uint32_t textRVA,
                                    uint32_t iatBaseRVA,
                                    const std::vector<PEImport> &imports,
                                    uint32_t dataBaseRVA) const;

    /// @brief Get the embedded data bytes (for .rdata or appended to existing .rdata).
    const std::vector<uint8_t> &dataSection() const {
        return data_;
    }

    /// @brief Current code size (before finalization).
    size_t codeSize() const {
        return code_.size();
    }

  private:
    std::vector<uint8_t> code_;
    std::vector<uint8_t> data_;

    struct LabelInfo {
        uint32_t codeOffset; ///< Offset in code_ where label is bound (0 if unbound).
        bool bound;
    };

    std::vector<LabelInfo> labels_;

    /// @brief Types of fixups that need resolution at finish time.
    enum class FixupKind : uint8_t {
        Rel32,        ///< 32-bit relative jump/call (code → code label)
        IATSlotIndex, ///< IAT slot by flat function index (resolved to RIP-relative)
        DataRel32,    ///< RIP-relative to embedded data (code → .rdata data section)
    };

    struct Fixup {
        uint32_t codeOffset; ///< Offset in code_ of the 4-byte displacement field.
        uint32_t target;     ///< Label ID / flat IAT index / data offset, depending on kind.
        FixupKind kind;
    };

    std::vector<Fixup> fixups_;

    // Encoding helpers
    void emit(uint8_t b);
    void emit32(uint32_t v);
    void emitREX(bool w, X64Reg reg, X64Reg rm);
    void emitModRM(uint8_t mod, uint8_t reg, uint8_t rm);
    void emitModRMDisp32(uint8_t reg, X64Reg base, int32_t disp);

    bool needsREX_B(X64Reg r) const {
        return static_cast<uint8_t>(r) >= 8;
    }

    uint8_t regBits(X64Reg r) const {
        return static_cast<uint8_t>(r) & 7;
    }
};

} // namespace viper::pkg
