//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/ObjectFileWriter.cpp
// Purpose: Factory for creating object file writers.
// Key invariants:
//   - Returns nullptr for unimplemented format/arch combinations
// Ownership/Lifetime:
//   - Caller owns the returned unique_ptr
// Links: codegen/common/objfile/ObjectFileWriter.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/objfile/ObjectFileWriter.hpp"
#include "codegen/common/objfile/CoffWriter.hpp"
#include "codegen/common/objfile/ElfWriter.hpp"
#include "codegen/common/objfile/MachOWriter.hpp"

namespace viper::codegen::objfile {

bool ObjectFileWriter::write(const std::string &path,
                             const std::vector<CodeSection> &textSections,
                             const CodeSection &rodata,
                             std::ostream &err) {
    // Default: merge all text sections into one and delegate to single-section write().
    CodeSection merged;
    for (const auto &ts : textSections)
        merged.appendSection(ts);
    return write(path, merged, rodata, err);
}

std::unique_ptr<ObjectFileWriter> createObjectFileWriter(ObjFormat format, ObjArch arch) {
    switch (format) {
        case ObjFormat::ELF:
            return std::make_unique<ElfWriter>(arch);
        case ObjFormat::MachO:
            return std::make_unique<MachOWriter>(arch);
        case ObjFormat::COFF:
            return std::make_unique<CoffWriter>(arch);
    }
    return nullptr;
}

} // namespace viper::codegen::objfile
