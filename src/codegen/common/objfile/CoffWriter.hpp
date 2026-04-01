//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/CoffWriter.hpp
// Purpose: COFF/PE object file writer for Windows (x86_64 and AArch64).
//          Produces valid .obj files that MSVC link.exe and LLD can consume.
// Key invariants:
//   - Produces IMAGE_FILE_MACHINE_AMD64 (0x8664) or IMAGE_FILE_MACHINE_ARM64 (0xAA64)
//   - 3 sections: .text, .rdata, .reloc (relocations inline after section data)
//   - Symbol names > 8 bytes use string table offset (/N format)
//   - String table has 4-byte size prefix (COFF convention)
//   - Relocations have no explicit addend (addend embedded in instruction bytes)
// Ownership/Lifetime:
//   - Created via ObjectFileWriter factory; caller owns the unique_ptr
// Links: codegen/common/objfile/ObjectFileWriter.hpp
//        plans/06-pe-coff-writer.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/objfile/ObjectFileWriter.hpp"

namespace viper::codegen::objfile {

/// COFF object file writer for Windows (x86_64 and AArch64).
class CoffWriter : public ObjectFileWriter {
  public:
    explicit CoffWriter(ObjArch arch) : arch_(arch) {}

    bool write(const std::string &path,
               const CodeSection &text,
               const CodeSection &rodata,
               std::ostream &err) override;

    bool write(const std::string &path,
               const std::vector<CodeSection> &textSections,
               const CodeSection &rodata,
               std::ostream &err) override;

  private:
    ObjArch arch_;
};

} // namespace viper::codegen::objfile
