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
#include "codegen/common/objfile/ElfWriter.hpp"
#include "codegen/common/objfile/MachOWriter.hpp"

namespace viper::codegen::objfile
{

std::unique_ptr<ObjectFileWriter> createObjectFileWriter(ObjFormat format, ObjArch arch)
{
    switch (format)
    {
    case ObjFormat::ELF:
        return std::make_unique<ElfWriter>(arch);
    case ObjFormat::MachO:
        return std::make_unique<MachOWriter>(arch);
    case ObjFormat::COFF:
        // Phase 6: CoffWriter
        return nullptr;
    }
    return nullptr;
}

} // namespace viper::codegen::objfile
