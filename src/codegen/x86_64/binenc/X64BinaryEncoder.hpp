//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/binenc/X64BinaryEncoder.hpp
// Purpose: Public interface for the x86_64 MIR-to-machine-code binary encoder.
//          Encodes MFunction basic blocks into raw bytes in a CodeSection,
//          resolving internal branches via patching and generating Relocations
//          for external symbols and cross-section references.
// Key invariants:
//   - All internal branches use rel32 (4-byte offsets), enabling single-pass
//     encoding with deferred patching
//   - External calls and RIP-relative data refs generate Relocation entries
//     with addend = -4 (standard x86_64 PC-relative convention)
//   - Pseudo-instructions (DIVS64rr, etc.) must be expanded before encoding
// Ownership/Lifetime:
//   - Encoder is stateless between encodeFunction() calls
//   - CodeSection is borrowed (caller retains ownership)
// Links: codegen/x86_64/binenc/X64Encoding.hpp
//        codegen/common/objfile/CodeSection.hpp
//        codegen/x86_64/MachineIR.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/objfile/CodeSection.hpp"
#include "codegen/x86_64/MachineIR.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper::codegen::x64::binenc
{

/// Encodes x86_64 MIR functions into machine code bytes.
///
/// Usage:
///   X64BinaryEncoder enc;
///   enc.encodeFunction(fn, textSection, rodataSection, isDarwin);
///
/// The encoder processes one function at a time. Internal branches are resolved
/// via patching after all blocks are emitted. External symbols generate
/// Relocation entries in the CodeSection.
class X64BinaryEncoder
{
public:
    /// Encode a complete MIR function into the text CodeSection.
    ///
    /// @param fn        The MIR function to encode.
    /// @param text      CodeSection for .text (machine code output).
    /// @param rodata    CodeSection for .rodata (used for RIP-relative symbol lookup).
    /// @param isDarwin  If true, symbol names get underscore-prefixed.
    void encodeFunction(const MFunction &fn,
                        objfile::CodeSection &text,
                        objfile::CodeSection &rodata,
                        bool isDarwin);

private:
    /// Encode a single MIR instruction.
    void encodeInstruction(const MInstr &instr,
                           objfile::CodeSection &text,
                           objfile::CodeSection &rodata,
                           bool isDarwin);

    // === Instruction encoding by category ===

    /// Encode nullary instructions (RET, CQO, UD2).
    void encodeNullary(MOpcode op, objfile::CodeSection &cs);

    /// Encode reg-reg GPR instructions (MOVrr, ADDrr, SUBrr, etc.).
    void encodeRegReg(MOpcode op, PhysReg dst, PhysReg src, objfile::CodeSection &cs);

    /// Encode reg-imm ALU instructions (ADDri, ANDri, CMPri, etc.).
    void encodeRegImm(MOpcode op, PhysReg dst, int64_t imm, objfile::CodeSection &cs);

    /// Encode shift instructions with immediate count.
    void encodeShiftImm(MOpcode op, PhysReg dst, int64_t count, objfile::CodeSection &cs);

    /// Encode shift instructions with CL register count.
    void encodeShiftCL(MOpcode op, PhysReg dst, objfile::CodeSection &cs);

    /// Encode unary division instructions (IDIVrm, DIVrm).
    void encodeDiv(MOpcode op, PhysReg src, objfile::CodeSection &cs);

    /// Encode MOVri (64-bit immediate move).
    void encodeMovRI(PhysReg dst, int64_t imm, objfile::CodeSection &cs);

    /// Encode memory-register (store) or register-memory (load) instructions.
    void encodeMemOp(MOpcode op, PhysReg reg, const OpMem &mem, objfile::CodeSection &cs);

    /// Encode LEA with memory operand.
    void encodeLEA(PhysReg dst, const OpMem &mem, objfile::CodeSection &cs);

    /// Encode LEA with RIP-relative label.
    void encodeLEARip(PhysReg dst, const OpRipLabel &rip,
                      objfile::CodeSection &text, objfile::CodeSection &rodata,
                      bool isDarwin);

    /// Encode SSE reg-reg instructions.
    void encodeSseRegReg(MOpcode op, PhysReg dst, PhysReg src, objfile::CodeSection &cs);

    /// Encode SSE memory instructions (MOVSDrm/MOVSDmr/MOVUPSrm/MOVUPSmr).
    void encodeSseMem(MOpcode op, PhysReg reg, const OpMem &mem, objfile::CodeSection &cs);

    /// Encode SETcc instruction.
    void encodeSETcc(int condCode, PhysReg dst, objfile::CodeSection &cs);

    /// Encode MOVZXrr32 (movzbq).
    void encodeMOVZX(PhysReg dst, PhysReg src, objfile::CodeSection &cs);

    /// Encode JMP/JCC/CALL with label target (direct branch/call).
    void encodeBranchLabel(MOpcode op, const std::string &label, int condCode,
                           objfile::CodeSection &cs);

    /// Encode JMP/CALL with register target (indirect).
    void encodeBranchReg(MOpcode op, PhysReg target, objfile::CodeSection &cs);

    /// Encode JMP/CALL with memory target (indirect via memory).
    void encodeBranchMem(MOpcode op, const OpMem &mem, objfile::CodeSection &cs);

    /// Encode CALL with external label (generates relocation).
    void encodeCallExternal(const std::string &name, objfile::CodeSection &cs, bool isDarwin);

    // === Low-level emission helpers ===

    /// Emit the ModR/M + optional SIB + displacement for a memory operand.
    /// @param reg3 The 3-bit reg field value (register or /ext).
    /// @param regRex REX bit for the reg field.
    /// @param mem The memory operand.
    /// @param cs Output code section.
    /// @param rexW Whether REX.W is needed.
    /// @param mandatoryPrefix SSE mandatory prefix (0xF2, 0x66, or 0 for none).
    /// @param opByte1 First opcode byte.
    /// @param opByte2 Second opcode byte (0 if single-byte opcode).
    void emitWithMemOperand(uint8_t reg3, uint8_t regRex,
                            const OpMem &mem,
                            objfile::CodeSection &cs,
                            bool rexW,
                            uint8_t mandatoryPrefix,
                            uint8_t opByte1,
                            uint8_t opByte2);

    // === Internal branch resolution ===

    /// A branch that needs its rel32 patched after all blocks are emitted.
    struct PendingBranch
    {
        size_t patchOffset;  ///< Offset in CodeSection of the rel32 placeholder.
        std::string target;  ///< Target label name.
    };

    /// Label name -> byte offset in CodeSection.
    std::unordered_map<std::string, size_t> labelOffsets_;

    /// Forward references needing patching.
    std::vector<PendingBranch> pendingBranches_;
};

} // namespace viper::codegen::x64::binenc
