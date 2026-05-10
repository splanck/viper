//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/RelocationValidation.hpp
// Purpose: Shared relocation validation helpers used by every object file
//          writer. Provides RelocKind→name lookup, an arch-compatibility test,
//          and a single validateRelocationShape() that every writer calls so
//          ELF/Mach-O/COFF report identical diagnostics on bad relocations.
// Key invariants:
//   - relocKindName() returns the canonical short name used in error messages.
//   - relocationFixupWidth() returns 4 for all 32-bit fixups and 8 for Abs64.
//   - validateRelocationShape() rejects archs/widths that overrun the section.
// Ownership/Lifetime: Stateless inline helpers — no allocation.
// Links: ElfWriter.cpp, MachOWriter.cpp, CoffWriter.cpp,
//        codegen/common/objfile/Relocation.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/objfile/ObjectFileWriter.hpp"

#include <cstddef>
#include <ostream>

namespace viper::codegen::objfile {

/// @brief Return the canonical short name for a RelocKind enum value.
/// @details Used in writer diagnostics so ELF/Mach-O/COFF print the same name
///          for the same kind, regardless of which format-specific code emitted it.
inline const char *relocKindName(RelocKind kind) {
    switch (kind) {
        case RelocKind::PCRel32:
            return "PCRel32";
        case RelocKind::Branch32:
            return "Branch32";
        case RelocKind::Abs64:
            return "Abs64";
        case RelocKind::A64Call26:
            return "A64Call26";
        case RelocKind::A64Jump26:
            return "A64Jump26";
        case RelocKind::A64AdrpPage21:
            return "A64AdrpPage21";
        case RelocKind::A64AddPageOff12:
            return "A64AddPageOff12";
        case RelocKind::A64LdSt32Off12:
            return "A64LdSt32Off12";
        case RelocKind::A64LdSt64Off12:
            return "A64LdSt64Off12";
        case RelocKind::A64LdSt128Off12:
            return "A64LdSt128Off12";
        case RelocKind::A64CondBr19:
            return "A64CondBr19";
    }
    return "<unknown>";
}

/// @brief Test whether a RelocKind is legal for the target architecture.
/// @details Catches frontend bugs that would emit (e.g.) A64Call26 into an
///          x86_64 object before the writer would silently encode garbage.
inline bool relocationKindMatchesArch(RelocKind kind, ObjArch arch) {
    switch (kind) {
        case RelocKind::PCRel32:
        case RelocKind::Branch32:
            return arch == ObjArch::X86_64;
        case RelocKind::Abs64:
            return arch == ObjArch::X86_64 || arch == ObjArch::AArch64;
        case RelocKind::A64Call26:
        case RelocKind::A64Jump26:
        case RelocKind::A64AdrpPage21:
        case RelocKind::A64AddPageOff12:
        case RelocKind::A64LdSt32Off12:
        case RelocKind::A64LdSt64Off12:
        case RelocKind::A64LdSt128Off12:
        case RelocKind::A64CondBr19:
            return arch == ObjArch::AArch64;
    }
    return false;
}

/// @brief Return the byte-width of the fixup field for a RelocKind.
/// @details All current AArch64 + x86_64 32-bit fixups return 4; Abs64 returns 8.
///          Used to verify that the relocation's offset doesn't overrun the
///          section before any writer-specific patching runs.
inline size_t relocationFixupWidth(RelocKind kind) {
    switch (kind) {
        case RelocKind::PCRel32:
        case RelocKind::Branch32:
        case RelocKind::A64Call26:
        case RelocKind::A64Jump26:
        case RelocKind::A64AdrpPage21:
        case RelocKind::A64AddPageOff12:
        case RelocKind::A64LdSt32Off12:
        case RelocKind::A64LdSt64Off12:
        case RelocKind::A64LdSt128Off12:
        case RelocKind::A64CondBr19:
            return 4;
        case RelocKind::Abs64:
            return 8;
    }
    return 0;
}

/// @brief One-stop shape check used by every object-file writer before patching.
/// @details Verifies (1) the relocation kind matches the architecture, and
///          (2) the fixup window stays inside the section. On any failure
///          writes a self-contained diagnostic to @p err and returns false.
/// @param writerName "ELF writer", "Mach-O writer", "COFF writer" — included in errors.
/// @param arch Target architecture for the object being produced.
/// @param section Section containing the relocation site.
/// @param rel The relocation to validate.
/// @param sectionName Human-readable section name for diagnostics (e.g. "__text").
/// @param err Stream to receive diagnostic output on failure.
inline bool validateRelocationShape(const char *writerName,
                                    ObjArch arch,
                                    const CodeSection &section,
                                    const Relocation &rel,
                                    const char *sectionName,
                                    std::ostream &err) {
    if (!relocationKindMatchesArch(rel.kind, arch)) {
        err << writerName << ": relocation kind " << relocKindName(rel.kind)
            << " is not valid for this object architecture in " << sectionName
            << " at offset " << rel.offset << "\n";
        return false;
    }

    const size_t width = relocationFixupWidth(rel.kind);
    if (!section.containsOffsetRange(rel.offset, width)) {
        err << writerName << ": relocation kind " << relocKindName(rel.kind) << " at offset "
            << rel.offset << " extends beyond " << sectionName << " contents\n";
        return false;
    }

    return true;
}

} // namespace viper::codegen::objfile
