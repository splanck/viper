//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/ElfWriter.hpp
// Purpose: ELF relocatable object file writer for x86_64 and AArch64.
//          Produces valid .o files that the system linker can consume.
// Key invariants:
//   - Produces ELFCLASS64, ELFDATA2LSB (little-endian), ET_REL files
//   - 8 sections: null, .text, .rodata, .rela.text, .symtab, .strtab,
//     .shstrtab, .note.GNU-stack
//   - Local symbols precede globals in .symtab (ELF requirement)
//   - Section symbols for .text and .rodata are emitted for cross-section relocs
// Ownership/Lifetime:
//   - Created via ObjectFileWriter factory; caller owns the unique_ptr
// Links: codegen/common/objfile/ObjectFileWriter.hpp
//        plans/04-elf-writer.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/objfile/ObjectFileWriter.hpp"

namespace viper::codegen::objfile {

/// ELF object file writer for Linux (x86_64 and AArch64).
class ElfWriter : public ObjectFileWriter {
  public:
    explicit ElfWriter(ObjArch arch) : arch_(arch) {}

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
