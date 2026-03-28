//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/RelocConstants.hpp
// Purpose: Named constants for relocation type numbers across ELF, Mach-O,
//          and COFF formats. Replaces raw integer literals in switch
//          statements and stub generators with self-documenting names.
// Key invariants:
//   - Values match their respective ABI specification exactly
//   - Format-prefixed names prevent collisions (e.g., ELF type 1 vs COFF type 1)
// Links: RelocApplier.cpp, DynStubGen.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace viper::codegen::linker {

// ── ELF x86_64 Relocation Types (elf.h / System V AMD64 ABI) ────────────

namespace elf_x64 {
constexpr uint32_t kAbs64 = 1;  // R_X86_64_64
constexpr uint32_t kPC32 = 2;   // R_X86_64_PC32
constexpr uint32_t kPLT32 = 4;  // R_X86_64_PLT32
constexpr uint32_t kAbs32 = 10; // R_X86_64_32
} // namespace elf_x64

// ── ELF AArch64 Relocation Types (ELF for ARM 64-bit Architecture) ──────

namespace elf_a64 {
constexpr uint32_t kAbs64 = 257;         // R_AARCH64_ABS64
constexpr uint32_t kAdrPrelPgHi21 = 275; // R_AARCH64_ADR_PREL_PG_HI21
constexpr uint32_t kAddAbsLo12Nc = 277;  // R_AARCH64_ADD_ABS_LO12_NC
constexpr uint32_t kCondBr19 = 280;      // R_AARCH64_CONDBR19
constexpr uint32_t kJump26 = 282;        // R_AARCH64_JUMP26
constexpr uint32_t kCall26 = 283;        // R_AARCH64_CALL26
constexpr uint32_t kLdSt32Lo12Nc = 285;  // R_AARCH64_LDST32_ABS_LO12_NC
constexpr uint32_t kLdSt64Lo12Nc = 286;  // R_AARCH64_LDST64_ABS_LO12_NC
constexpr uint32_t kLdSt128Lo12Nc = 299; // R_AARCH64_LDST128_ABS_LO12_NC
} // namespace elf_a64

// ── Mach-O x86_64 Relocation Types (mach-o/x86_64/reloc.h) ─────────────

namespace macho_x64 {
constexpr uint32_t kUnsigned = 0; // X86_64_RELOC_UNSIGNED
constexpr uint32_t kSigned = 1;   // X86_64_RELOC_SIGNED
constexpr uint32_t kBranch = 2;   // X86_64_RELOC_BRANCH
} // namespace macho_x64

// ── Mach-O ARM64 Relocation Types (mach-o/arm64/reloc.h) ────────────────

namespace macho_a64 {
constexpr uint32_t kUnsigned = 0;          // ARM64_RELOC_UNSIGNED
constexpr uint32_t kBranch26 = 2;          // ARM64_RELOC_BRANCH26
constexpr uint32_t kPage21 = 3;            // ARM64_RELOC_PAGE21
constexpr uint32_t kPageOff12 = 4;         // ARM64_RELOC_PAGEOFF12
constexpr uint32_t kGotLoadPage21 = 5;     // ARM64_RELOC_GOT_LOAD_PAGE21
constexpr uint32_t kGotLoadPageOff12 = 6;  // ARM64_RELOC_GOT_LOAD_PAGEOFF12
constexpr uint32_t kTlvpLoadPage21 = 8;    // ARM64_RELOC_TLVP_LOAD_PAGE21
constexpr uint32_t kTlvpLoadPageOff12 = 9; // ARM64_RELOC_TLVP_LOAD_PAGEOFF12
} // namespace macho_a64

// ── COFF AMD64 Relocation Types (winnt.h) ───────────────────────────────

namespace coff_x64 {
constexpr uint32_t kAddr64 = 1; // IMAGE_REL_AMD64_ADDR64
constexpr uint32_t kAddr32 = 2; // IMAGE_REL_AMD64_ADDR32
constexpr uint32_t kRel32 = 4;  // IMAGE_REL_AMD64_REL32
} // namespace coff_x64

// ── COFF ARM64 Relocation Types (winnt.h) ───────────────────────────────

namespace coff_a64 {
constexpr uint32_t kBranch26 = 3;   // IMAGE_REL_ARM64_BRANCH26
constexpr uint32_t kPageRel21 = 4;  // IMAGE_REL_ARM64_PAGEBASE_REL21
constexpr uint32_t kPageOff12A = 6; // IMAGE_REL_ARM64_PAGEOFFSET_12A
constexpr uint32_t kPageOff12L = 7; // IMAGE_REL_ARM64_PAGEOFFSET_12L
constexpr uint32_t kBranch19 = 8;   // IMAGE_REL_ARM64_BRANCH19
} // namespace coff_a64

} // namespace viper::codegen::linker
