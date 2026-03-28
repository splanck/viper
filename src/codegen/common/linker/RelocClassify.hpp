//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/RelocClassify.hpp
// Purpose: Map format-specific relocation type numbers to format-independent
//          RelocAction categories. Separates relocation classification from
//          the patching logic in RelocApplier.cpp.
// Key invariants:
//   - One mapping function per (format × architecture) combination
//   - classifyReloc() dispatches by format + arch to the correct mapper
//   - Uses named constants from RelocConstants.hpp (no raw integers)
// Links: RelocApplier.cpp, RelocConstants.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/RelocConstants.hpp"

#include <cstdint>

namespace viper::codegen::linker {

/// Format-independent relocation categories.
enum class RelocAction {
    PCRel32,      // S + A - P (32-bit)
    Abs64,        // S + A (64-bit)
    Abs32,        // S + A (32-bit)
    Branch26,     // AArch64: ((S+A-P)>>2) & 0x3FFFFFF
    Page21,       // AArch64: ADRP page delta
    PageOff12,    // AArch64: ADD page offset
    LdSt64Off,    // AArch64: LDR/STR 64-bit scaled offset
    LdSt32Off,    // AArch64: LDR/STR 32-bit scaled offset
    LdSt128Off,   // AArch64: LDR/STR 128-bit scaled offset
    CondBr19,     // AArch64: B.cond 19-bit
    GotPage21,    // AArch64: GOT ADRP (relaxable to Page21 for local symbols)
    GotPageOff12, // AArch64: GOT LDR pageoff (relaxable to ADD for local symbols)
    Unknown,
};

// ── Per-format mapping functions ─────────────────────────────────────────

inline RelocAction elfX64Action(uint32_t type) {
    switch (type) {
        case elf_x64::kAbs64:
            return RelocAction::Abs64;
        case elf_x64::kPC32:
            return RelocAction::PCRel32;
        case elf_x64::kPLT32:
            return RelocAction::PCRel32;
        case elf_x64::kAbs32:
            return RelocAction::Abs32;
        default:
            return RelocAction::Unknown;
    }
}

inline RelocAction elfA64Action(uint32_t type) {
    switch (type) {
        case elf_a64::kAbs64:
            return RelocAction::Abs64;
        case elf_a64::kAdrPrelPgHi21:
            return RelocAction::Page21;
        case elf_a64::kAddAbsLo12Nc:
            return RelocAction::PageOff12;
        case elf_a64::kCondBr19:
            return RelocAction::CondBr19;
        case elf_a64::kJump26:
            return RelocAction::Branch26;
        case elf_a64::kCall26:
            return RelocAction::Branch26;
        case elf_a64::kLdSt32Lo12Nc:
            return RelocAction::LdSt32Off;
        case elf_a64::kLdSt64Lo12Nc:
            return RelocAction::LdSt64Off;
        case elf_a64::kLdSt128Lo12Nc:
            return RelocAction::LdSt128Off;
        default:
            return RelocAction::Unknown;
    }
}

inline RelocAction machoX64Action(uint32_t type) {
    switch (type) {
        case macho_x64::kUnsigned:
            return RelocAction::Abs64;
        case macho_x64::kSigned:
            return RelocAction::PCRel32;
        case macho_x64::kBranch:
            return RelocAction::PCRel32;
        default:
            return RelocAction::Unknown;
    }
}

inline RelocAction machoA64Action(uint32_t type) {
    switch (type) {
        case macho_a64::kUnsigned:
            return RelocAction::Abs64;
        case macho_a64::kBranch26:
            return RelocAction::Branch26;
        case macho_a64::kPage21:
            return RelocAction::Page21;
        case macho_a64::kPageOff12:
            return RelocAction::PageOff12;
        case macho_a64::kGotLoadPage21:
            return RelocAction::GotPage21;
        case macho_a64::kGotLoadPageOff12:
            return RelocAction::GotPageOff12;
        case macho_a64::kTlvpLoadPage21:
            return RelocAction::Page21;
        case macho_a64::kTlvpLoadPageOff12:
            // TLV descriptor address known at link time. Rewrite LDR to ADD
            // (GOT relaxation style) so the code gets a pointer TO the descriptor,
            // not a load FROM it.
            return RelocAction::GotPageOff12;
        default:
            return RelocAction::Unknown;
    }
}

inline RelocAction coffX64Action(uint32_t type) {
    switch (type) {
        case coff_x64::kAddr64:
            return RelocAction::Abs64;
        case coff_x64::kAddr32:
            return RelocAction::Abs32;
        case coff_x64::kRel32:
            return RelocAction::PCRel32;
        default:
            return RelocAction::Unknown;
    }
}

inline RelocAction coffA64Action(uint32_t type) {
    switch (type) {
        case coff_a64::kBranch26:
            return RelocAction::Branch26;
        case coff_a64::kPageRel21:
            return RelocAction::Page21;
        case coff_a64::kPageOff12A:
            return RelocAction::PageOff12;
        case coff_a64::kPageOff12L:
            return RelocAction::LdSt64Off;
        case coff_a64::kBranch19:
            return RelocAction::CondBr19;
        default:
            return RelocAction::Unknown;
    }
}

// ── Top-level dispatcher ─────────────────────────────────────────────────

/// Dispatch relocation type to action based on format and architecture.
inline RelocAction classifyReloc(ObjFileFormat format, LinkArch arch, uint32_t type) {
    switch (format) {
        case ObjFileFormat::ELF:
            return (arch == LinkArch::X86_64) ? elfX64Action(type) : elfA64Action(type);
        case ObjFileFormat::MachO:
            return (arch == LinkArch::X86_64) ? machoX64Action(type) : machoA64Action(type);
        case ObjFileFormat::COFF:
            return (arch == LinkArch::X86_64) ? coffX64Action(type) : coffA64Action(type);
        default:
            return RelocAction::Unknown;
    }
}

} // namespace viper::codegen::linker
