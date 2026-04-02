//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/MachOWriter.hpp
// Purpose: Mach-O relocatable object file writer for x86_64 and AArch64.
//          Produces valid .o files that Apple's ld64 linker can consume.
// Key invariants:
//   - Produces MH_MAGIC_64 MH_OBJECT files with LC_SEGMENT_64, LC_BUILD_VERSION,
//     LC_SYMTAB, and LC_DYSYMTAB load commands
//   - Two sections: __TEXT,__text (code) and __TEXT,__const (read-only data)
//   - Symbol names get underscore prefix (Darwin convention)
//   - Relocations sorted in descending address order per section
//   - Addends embedded in instruction bytes (Mach-O REL, not RELA)
// Ownership/Lifetime:
//   - Created via ObjectFileWriter factory; caller owns the unique_ptr
// Links: codegen/common/objfile/ObjectFileWriter.hpp
//        plans/05-macho-writer.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/objfile/ObjectFileWriter.hpp"

namespace viper::codegen::objfile {

/// Mach-O object file writer for macOS (x86_64 and AArch64).
class MachOWriter : public ObjectFileWriter {
  public:
    explicit MachOWriter(ObjArch arch) : arch_(arch) {}

    /// @brief Write a Mach-O .o file with __TEXT,__text and __TEXT,__const sections.
    bool write(const std::string &path,
               const CodeSection &text,
               const CodeSection &rodata,
               std::ostream &err) override;

  private:
    ObjArch arch_;
};

} // namespace viper::codegen::objfile
