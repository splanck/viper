//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/binenc/A64BinaryEncoder.hpp
// Purpose: Public interface for the AArch64 MIR-to-machine-code binary encoder.
//          Encodes MFunction basic blocks into raw 32-bit instruction words in a
//          CodeSection, synthesizing prologue/epilogue at emission time and
//          resolving internal branches via patching.
// Key invariants:
//   - Every instruction emitted is exactly 4 bytes (32 bits)
//   - Prologue/epilogue are synthesized from MFunction metadata (not MIR instrs)
//   - External calls generate A64Call26 relocations; ADRP/ADD generate page relocs
//   - Pseudo-instructions (overflow-checked ops) must be expanded before encoding
//   - Symbol mangling is NOT done here; deferred to ObjectFileWriter
// Ownership/Lifetime:
//   - Encoder is stateless between encodeFunction() calls
//   - CodeSection is borrowed (caller retains ownership)
// Links: codegen/aarch64/binenc/A64Encoding.hpp
//        codegen/common/objfile/CodeSection.hpp
//        codegen/aarch64/MachineIR.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"
#include "codegen/common/objfile/CodeSection.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper::codegen
{
class DebugLineTable;
}

namespace viper::codegen::aarch64::binenc
{

using viper::codegen::DebugLineTable;

/// Encodes AArch64 MIR functions into machine code bytes.
///
/// Usage:
///   A64BinaryEncoder enc;
///   enc.encodeFunction(fn, textSection, rodataSection, ABIFormat::Darwin);
///
/// The encoder processes one function at a time. Prologue and epilogue are
/// synthesized from MFunction::savedGPRs/savedFPRs/localFrameSize (not from
/// MIR instructions). Internal branches are resolved via patching. External
/// symbols generate Relocation entries.
class A64BinaryEncoder
{
  public:
    /// Set the debug line table for recording address→line mappings.
    /// @param table Pointer to the DebugLineTable (null disables recording).
    void setDebugLineTable(DebugLineTable *table)
    {
        debugLines_ = table;
    }

    /// Encode a complete MIR function into the text CodeSection.
    ///
    /// @param fn      The MIR function to encode.
    /// @param text    CodeSection for .text (machine code output).
    /// @param rodata  CodeSection for .rodata (literal pool — currently unused).
    /// @param abi     ABI format controlling runtime symbol mapping.
    void encodeFunction(const MFunction &fn,
                        objfile::CodeSection &text,
                        objfile::CodeSection &rodata,
                        ABIFormat abi);

  private:
    /// Encode a single MIR instruction.
    void encodeInstruction(const MInstr &mi, objfile::CodeSection &cs);

    // === Prologue/epilogue synthesis ===

    /// Emit function prologue: save FP/LR, allocate frame, save callee-saved regs.
    void encodePrologue(const MFunction &fn, objfile::CodeSection &cs);

    /// Emit function epilogue: restore callee-saved, deallocate frame, restore FP/LR, ret.
    void encodeEpilogue(const MFunction &fn, objfile::CodeSection &cs);

    /// Emit runtime init calls for main function (bl rt_legacy_context + rt_set_current_context).
    void encodeMainInit(objfile::CodeSection &cs);

    // === Multi-instruction sequences ===

    /// Emit movz + movk sequence for a 64-bit immediate.
    void encodeMovImm64(uint32_t rd, uint64_t imm, objfile::CodeSection &cs);

    /// Emit sub sp, sp, #bytes (chunked if >4080).
    void encodeSubSp(int64_t bytes, objfile::CodeSection &cs);

    /// Emit add sp, sp, #bytes (chunked if >4080).
    void encodeAddSp(int64_t bytes, objfile::CodeSection &cs);

    /// Emit a large-offset load/store sequence using scratch X9.
    void encodeLargeOffsetLdSt(uint32_t rt,
                               uint32_t base,
                               int64_t offset,
                               bool isLoad,
                               bool isFPR,
                               objfile::CodeSection &cs);

    /// Emit a single 32-bit instruction word.
    void emit32(uint32_t word, objfile::CodeSection &cs)
    {
        cs.emit32LE(word);
    }

    // === Internal branch resolution ===

    struct PendingBranch
    {
        size_t offset;      ///< Byte offset in CodeSection of the instruction.
        std::string target; ///< Target label name.
        MOpcode kind;       ///< Branch type (Br/BCond/Cbz/Cbnz/Bl).
    };

    /// Label name → byte offset in CodeSection.
    std::unordered_map<std::string, size_t> labelOffsets_;

    /// Forward references needing patching.
    std::vector<PendingBranch> pendingBranches_;

    /// Whether this function's prologue uses a FramePlan (has callee-saved or locals).
    bool usePlan_{false};

    /// Whether prologue/epilogue should be skipped (leaf function optimization).
    bool skipFrame_{false};

    /// Pointer to current function being encoded (for epilogue synthesis on Ret).
    const MFunction *currentFn_{nullptr};

    /// Optional debug line table for recording address→line mappings.
    DebugLineTable *debugLines_{nullptr};
};

} // namespace viper::codegen::aarch64::binenc
