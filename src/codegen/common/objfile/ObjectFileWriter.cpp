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
    for (const auto &ts : textSections) {
        // Copy symbols from this text section into the merged section.
        for (uint32_t i = 1; i < ts.symbols().count(); ++i) {
            const auto &sym = ts.symbols().at(i);
            if (sym.binding == SymbolBinding::External || sym.section == SymbolSection::Undefined)
                merged.findOrDeclareSymbol(sym.name);
            else
                merged.defineSymbol(sym.name, sym.binding, sym.section);
        }

        // Record the base offset for relocations from this section.
        size_t baseOffset = merged.currentOffset();

        // Copy bytes.
        merged.emitBytes(ts.bytes().data(), ts.bytes().size());

        // Copy relocations, adjusting offsets and remapping symbol indices.
        for (const auto &rel : ts.relocations()) {
            const auto &sym = ts.symbols().at(rel.symbolIndex);
            uint32_t mergedSymIdx = merged.findOrDeclareSymbol(sym.name);
            merged.addRelocationAt(baseOffset + rel.offset, rel.kind, mergedSymIdx, rel.addend);
        }

        // Copy compact unwind entries, remapping symbol indices.
        for (const auto &ue : ts.unwindEntries()) {
            const auto &sym = ts.symbols().at(ue.symbolIndex);
            uint32_t mergedSymIdx = merged.findOrDeclareSymbol(sym.name);
            merged.addUnwindEntry({mergedSymIdx, ue.functionLength, ue.encoding});
        }

        // Copy Win64 unwind entries, remapping symbol indices.
        for (const auto &ue : ts.win64UnwindEntries()) {
            const auto &sym = ts.symbols().at(ue.symbolIndex);
            uint32_t mergedSymIdx = merged.findOrDeclareSymbol(sym.name);

            Win64UnwindEntry mergedEntry{};
            mergedEntry.symbolIndex = mergedSymIdx;
            mergedEntry.functionLength = ue.functionLength;
            mergedEntry.prologueSize = ue.prologueSize;
            mergedEntry.codes = ue.codes;
            merged.addWin64UnwindEntry(std::move(mergedEntry));
        }
    }
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
