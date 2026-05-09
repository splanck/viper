//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/RelocationValidation.hpp
// Purpose: Shared relocation validation for object file writers.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/objfile/ObjectFileWriter.hpp"

#include <cstddef>
#include <ostream>

namespace viper::codegen::objfile {

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
        case RelocKind::A64LdSt64Off12:
            return "A64LdSt64Off12";
        case RelocKind::A64CondBr19:
            return "A64CondBr19";
    }
    return "<unknown>";
}

inline bool relocationKindMatchesArch(RelocKind kind, ObjArch arch) {
    switch (kind) {
        case RelocKind::PCRel32:
        case RelocKind::Branch32:
        case RelocKind::Abs64:
            return arch == ObjArch::X86_64;
        case RelocKind::A64Call26:
        case RelocKind::A64Jump26:
        case RelocKind::A64AdrpPage21:
        case RelocKind::A64AddPageOff12:
        case RelocKind::A64LdSt64Off12:
        case RelocKind::A64CondBr19:
            return arch == ObjArch::AArch64;
    }
    return false;
}

inline size_t relocationFixupWidth(RelocKind kind) {
    switch (kind) {
        case RelocKind::PCRel32:
        case RelocKind::Branch32:
        case RelocKind::A64Call26:
        case RelocKind::A64Jump26:
        case RelocKind::A64AdrpPage21:
        case RelocKind::A64AddPageOff12:
        case RelocKind::A64LdSt64Off12:
        case RelocKind::A64CondBr19:
            return 4;
        case RelocKind::Abs64:
            return 8;
    }
    return 0;
}

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
