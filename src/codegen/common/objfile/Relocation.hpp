//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/Relocation.hpp
// Purpose: Architecture-agnostic relocation types and structures for the
//          native assembler. Each object file writer (ELF, Mach-O, COFF)
//          maps these to format-specific relocation codes.
// Key invariants:
//   - RelocKind is architecture-prefixed (A64* for AArch64) to avoid confusion
//   - Addend is stored ELF RELA-style; Mach-O writer patches into instruction
//   - x86_64 PCRel32/Branch32 always use addend = -4
// Ownership/Lifetime:
//   - Relocation structs are value types stored in CodeSection vectors
// Links: codegen/common/objfile/CodeSection.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>

namespace viper::codegen::objfile {

/// Architecture-agnostic relocation kinds.
///
/// Each object file writer maps these to format-specific codes:
///   ELF:    R_X86_64_* / R_AARCH64_*
///   Mach-O: X86_64_RELOC_* / ARM64_RELOC_*
///   COFF:   IMAGE_REL_AMD64_* / IMAGE_REL_ARM64_*
enum class RelocKind : uint8_t {
    // === x86_64 relocations ===

    /// 32-bit PC-relative (RIP-relative data references).
    ///   ELF:    R_X86_64_PC32 (2)
    ///   Mach-O: X86_64_RELOC_SIGNED (1)
    ///   COFF:   IMAGE_REL_AMD64_REL32 (4)
    PCRel32,

    /// 32-bit PC-relative call/branch (external symbols).
    ///   ELF:    R_X86_64_PLT32 (4)
    ///   Mach-O: X86_64_RELOC_BRANCH (2)
    ///   COFF:   IMAGE_REL_AMD64_REL32 (4)
    Branch32,

    /// 64-bit absolute address.
    ///   ELF:    R_X86_64_64 (1)
    ///   Mach-O: X86_64_RELOC_UNSIGNED (0)
    ///   COFF:   IMAGE_REL_AMD64_ADDR64 (1)
    Abs64,

    // === AArch64 relocations ===

    /// BL 26-bit PC-relative.
    ///   ELF:    R_AARCH64_CALL26 (283)
    ///   Mach-O: ARM64_RELOC_BRANCH26 (2)
    ///   COFF:   IMAGE_REL_ARM64_BRANCH26 (3)
    A64Call26,

    /// B 26-bit PC-relative.
    ///   ELF:    R_AARCH64_JUMP26 (282)
    ///   Mach-O: ARM64_RELOC_BRANCH26 (2)
    ///   COFF:   IMAGE_REL_ARM64_BRANCH26 (3)
    A64Jump26,

    /// ADRP 21-bit page-relative.
    ///   ELF:    R_AARCH64_ADR_PREL_PG_HI21 (275)
    ///   Mach-O: ARM64_RELOC_PAGE21 (3)
    ///   COFF:   IMAGE_REL_ARM64_PAGEBASE_REL21 (4)
    A64AdrpPage21,

    /// ADD 12-bit page offset (no carry).
    ///   ELF:    R_AARCH64_ADD_ABS_LO12_NC (277)
    ///   Mach-O: ARM64_RELOC_PAGEOFF12 (4)
    ///   COFF:   IMAGE_REL_ARM64_PAGEOFFSET_12A (6)
    A64AddPageOff12,

    /// LDR/STR 12-bit page offset scaled by 8.
    ///   ELF:    R_AARCH64_LDST64_ABS_LO12_NC (286)
    ///   Mach-O: ARM64_RELOC_PAGEOFF12 (4)
    ///   COFF:   IMAGE_REL_ARM64_PAGEOFFSET_12L (7)
    A64LdSt64Off12,

    /// B.cond/CBZ/CBNZ 19-bit PC-relative.
    ///   ELF:    R_AARCH64_CONDBR19 (280)
    ///   Mach-O: Not used (conditional branches always resolve internally)
    ///   COFF:   IMAGE_REL_ARM64_BRANCH19 (8)
    A64CondBr19,
};

/// A relocation entry recorded during binary encoding.
struct Relocation {
    size_t offset;        ///< Byte offset in section where fixup applies.
    RelocKind kind;       ///< Architecture-agnostic relocation type.
    uint32_t symbolIndex; ///< Index into SymbolTable.
    int64_t addend;       ///< Addend (ELF RELA style; Mach-O embeds in instruction).
};

} // namespace viper::codegen::objfile
