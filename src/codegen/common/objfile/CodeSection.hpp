//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/CodeSection.hpp
// Purpose: Growable byte buffer that accumulates machine code or read-only data
//          while tracking relocations and symbol definitions.
// Key invariants:
//   - Little-endian only (both x86_64 and AArch64 targets use LE)
//   - Internal branch resolution uses patch32LE(); only external references
//     and cross-section references generate Relocation entries
//   - Separate CodeSection instances for .text and .rodata
// Ownership/Lifetime:
//   - Owned by the binary encoder; passed to ObjectFileWriter for serialization
// Links: codegen/common/objfile/Relocation.hpp
//        codegen/common/objfile/SymbolTable.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/objfile/Relocation.hpp"
#include "codegen/common/objfile/SymbolTable.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace viper::codegen::objfile {

/// Per-function compact unwind entry for Mach-O __compact_unwind section.
///
/// Collected during binary encoding and serialized into the object file by
/// the Mach-O writer. Each entry is 32 bytes in the output file.
struct CompactUnwindEntry {
    uint32_t symbolIndex{0};    ///< Symbol index of the function start.
    uint32_t functionLength{0}; ///< Length of the function in bytes.
    uint32_t encoding{0};       ///< ARM64/x86_64 compact unwind encoding.
};

/// One concrete Win64 unwind opcode emitted into PE `.xdata`.
struct Win64UnwindCode {
    enum class Kind : uint8_t {
        PushNonVol,
        AllocStack,
        SaveNonVol,
        SaveXmm128,
    };

    Kind kind{Kind::AllocStack};
    uint8_t codeOffset{0};   ///< End offset of the corresponding prologue instruction.
    uint8_t reg{0};          ///< Win64 register number for save/push operations.
    uint32_t stackOffset{0}; ///< Offset from final RSP after the prologue.
};

/// Per-function Win64 unwind metadata for COFF `.xdata/.pdata`.
struct Win64UnwindEntry {
    uint32_t symbolIndex{0};    ///< Symbol index of the function start.
    uint32_t functionLength{0}; ///< Length of the function in bytes.
    uint8_t prologueSize{0};    ///< Size of the function prologue in bytes.
    std::vector<Win64UnwindCode> codes{};
};

/// A growable byte buffer with relocation and symbol tracking.
///
/// Used by binary encoders to accumulate machine code (.text) or read-only
/// data (.rodata). The ObjectFileWriter receives one or more CodeSections
/// and serializes them into the target object file format.
class CodeSection {
  public:
    // === Byte emission ===

    /// Current write position (byte offset from start of section).
    size_t currentOffset() const {
        return bytes_.size();
    }

    /// Append a single byte.
    void emit8(uint8_t val) {
        bytes_.push_back(val);
    }

    /// Append 2 bytes, little-endian.
    void emit16LE(uint16_t val) {
        bytes_.push_back(static_cast<uint8_t>(val));
        bytes_.push_back(static_cast<uint8_t>(val >> 8));
    }

    /// Append 4 bytes, little-endian.
    void emit32LE(uint32_t val) {
        bytes_.push_back(static_cast<uint8_t>(val));
        bytes_.push_back(static_cast<uint8_t>(val >> 8));
        bytes_.push_back(static_cast<uint8_t>(val >> 16));
        bytes_.push_back(static_cast<uint8_t>(val >> 24));
    }

    /// Append 8 bytes, little-endian.
    void emit64LE(uint64_t val) {
        bytes_.push_back(static_cast<uint8_t>(val));
        bytes_.push_back(static_cast<uint8_t>(val >> 8));
        bytes_.push_back(static_cast<uint8_t>(val >> 16));
        bytes_.push_back(static_cast<uint8_t>(val >> 24));
        bytes_.push_back(static_cast<uint8_t>(val >> 32));
        bytes_.push_back(static_cast<uint8_t>(val >> 40));
        bytes_.push_back(static_cast<uint8_t>(val >> 48));
        bytes_.push_back(static_cast<uint8_t>(val >> 56));
    }

    /// Append arbitrary bytes.
    void emitBytes(const void *data, size_t len) {
        auto ptr = static_cast<const uint8_t *>(data);
        bytes_.insert(bytes_.end(), ptr, ptr + len);
    }

    /// Append N zero bytes (for alignment padding).
    void emitZeros(size_t count) {
        bytes_.insert(bytes_.end(), count, 0);
    }

    // === Alignment ===

    /// Pad with zeros to reach the given alignment boundary.
    void alignTo(size_t alignment) {
        size_t rem = bytes_.size() % alignment;
        if (rem != 0)
            emitZeros(alignment - rem);
    }

    // === Relocation tracking ===

    /// Record a relocation at the current offset.
    void addRelocation(RelocKind kind, uint32_t symbolIndex, int64_t addend = 0) {
        relocations_.push_back(Relocation{currentOffset(), kind, symbolIndex, addend});
    }

    /// Record a relocation at a specific offset.
    void addRelocationAt(size_t offset, RelocKind kind, uint32_t symbolIndex, int64_t addend = 0) {
        relocations_.push_back(Relocation{offset, kind, symbolIndex, addend});
    }

    // === Symbol management ===

    /// Define a symbol at the current offset. Returns its index in the symbol table.
    uint32_t defineSymbol(const std::string &name, SymbolBinding binding, SymbolSection section) {
        return symbols_.add(Symbol{name, binding, section, currentOffset(), 0});
    }

    /// Declare an external (undefined) symbol. Returns its index.
    uint32_t declareExternal(const std::string &name) {
        return symbols_.findOrAdd(name);
    }

    /// Find an existing symbol or declare it as external. Returns its index.
    uint32_t findOrDeclareSymbol(const std::string &name) {
        return symbols_.findOrAdd(name);
    }

    // === Patch (for resolved internal branches) ===

    /// Overwrite 4 bytes at the given offset (little-endian).
    void patch32LE(size_t offset, uint32_t val) {
        bytes_[offset] = static_cast<uint8_t>(val);
        bytes_[offset + 1] = static_cast<uint8_t>(val >> 8);
        bytes_[offset + 2] = static_cast<uint8_t>(val >> 16);
        bytes_[offset + 3] = static_cast<uint8_t>(val >> 24);
    }

    /// Overwrite 1 byte at the given offset.
    void patch8(size_t offset, uint8_t val) {
        bytes_[offset] = val;
    }

    // === Accessors ===

    /// Raw byte buffer.
    const std::vector<uint8_t> &bytes() const {
        return bytes_;
    }

    /// Mutable byte buffer (for Mach-O addend embedding).
    std::vector<uint8_t> &mutableBytes() {
        return bytes_;
    }

    /// All recorded relocations.
    const std::vector<Relocation> &relocations() const {
        return relocations_;
    }

    /// The symbol table.
    const SymbolTable &symbols() const {
        return symbols_;
    }

    /// Mutable symbol table.
    SymbolTable &symbols() {
        return symbols_;
    }

    /// Append another CodeSection, rebasing symbol offsets and relocation sites.
    void appendSection(const CodeSection &other) {
        const size_t offsetBias = currentOffset();
        std::vector<uint32_t> symbolRemap(other.symbols().count(), 0);

        for (uint32_t i = 1; i < other.symbols().count(); ++i) {
            const Symbol &sym = other.symbols().at(i);
            uint32_t mappedIndex = 0;

            if (sym.binding == SymbolBinding::External || sym.section == SymbolSection::Undefined) {
                mappedIndex = findOrDeclareSymbol(sym.name);
            } else {
                mappedIndex = symbols_.findOrAdd(sym.name);
                Symbol &dst = symbols_.at(mappedIndex);
                dst.binding = sym.binding;
                dst.section = sym.section;
                dst.offset = offsetBias + sym.offset;
                dst.size = sym.size;
            }

            symbolRemap[i] = mappedIndex;
        }

        if (!other.bytes().empty())
            emitBytes(other.bytes().data(), other.bytes().size());

        for (const auto &reloc : other.relocations()) {
            addRelocationAt(offsetBias + reloc.offset,
                            reloc.kind,
                            symbolRemap[reloc.symbolIndex],
                            reloc.addend);
        }

        for (const auto &entry : other.unwindEntries()) {
            CompactUnwindEntry rebased = entry;
            rebased.symbolIndex = symbolRemap[entry.symbolIndex];
            unwindEntries_.push_back(rebased);
        }

        for (const auto &entry : other.win64UnwindEntries()) {
            Win64UnwindEntry rebased = entry;
            rebased.symbolIndex = symbolRemap[entry.symbolIndex];
            win64UnwindEntries_.push_back(std::move(rebased));
        }
    }

    /// Whether this section has any content.
    bool empty() const {
        return bytes_.empty();
    }

    // === Compact unwind tracking ===

    /// Record a compact unwind entry for a function.
    void addUnwindEntry(const CompactUnwindEntry &entry) {
        unwindEntries_.push_back(entry);
    }

    /// All recorded compact unwind entries.
    const std::vector<CompactUnwindEntry> &unwindEntries() const {
        return unwindEntries_;
    }

    // === Win64 unwind tracking ===

    /// Record a Win64 unwind entry for a function.
    void addWin64UnwindEntry(Win64UnwindEntry entry) {
        win64UnwindEntries_.push_back(std::move(entry));
    }

    /// All recorded Win64 unwind entries.
    const std::vector<Win64UnwindEntry> &win64UnwindEntries() const {
        return win64UnwindEntries_;
    }

  private:
    std::vector<uint8_t> bytes_;
    std::vector<Relocation> relocations_;
    SymbolTable symbols_;
    std::vector<CompactUnwindEntry> unwindEntries_;
    std::vector<Win64UnwindEntry> win64UnwindEntries_;
};

} // namespace viper::codegen::objfile
