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

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
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

/// Per-function Windows ARM64 unwind metadata for COFF `.xdata/.pdata`.
///
/// The unwind byte stream is stored in Windows' ARM64 xdata opcode format and
/// serialized by the COFF writer behind an ARM64 pdata entry.
struct WinArm64UnwindEntry {
    uint32_t symbolIndex{0};    ///< Symbol index of the function start.
    uint32_t functionLength{0}; ///< Length of the function in bytes.
    uint8_t prologueSize{0};    ///< Prologue size in bytes, for validation/debugging.
    std::vector<uint8_t> unwindCodes{};
    bool packedEpilogInHeader{true};
    uint8_t epilogCodeIndex{0};
};

/// @brief Return the encoded fixup width for a relocation kind.
/// @details Object writers use this information when validating and patching
///          relocation payloads. Relocations may be recorded before their
///          placeholder bytes are emitted, so CodeSection validates the source
///          offset itself and leaves full-width checks to serialization.
/// @param kind Architecture-independent relocation kind.
/// @return Number of bytes occupied by the fixup field.
inline size_t codeSectionRelocationFixupWidth(RelocKind kind) {
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

/// A growable byte buffer with relocation and symbol tracking.
///
/// Used by binary encoders to accumulate machine code (.text) or read-only
/// data (.rodata). The ObjectFileWriter receives one or more CodeSections
/// and serializes them into the target object file format.
class CodeSection {
  public:
    CodeSection() : sectionIdentity_(allocateSectionIdentity()) {}

    CodeSection(const CodeSection &other)
        : bytes_(other.bytes_), relocations_(other.relocations_), symbols_(other.symbols_),
          unwindEntries_(other.unwindEntries_), win64UnwindEntries_(other.win64UnwindEntries_),
          winArm64UnwindEntries_(other.winArm64UnwindEntries_), offsetBias_(other.offsetBias_),
          sectionIdentityAliases_(other.sectionIdentityAliases_),
          sectionIdentity_(allocateSectionIdentity()) {
        addSectionIdentityAlias(other.sectionIdentity_);
        retargetSelfIdentity(other.sectionIdentity_, sectionIdentity_);
    }

    CodeSection(CodeSection &&other)
        : bytes_(std::move(other.bytes_)), relocations_(std::move(other.relocations_)),
          symbols_(std::move(other.symbols_)), unwindEntries_(std::move(other.unwindEntries_)),
          win64UnwindEntries_(std::move(other.win64UnwindEntries_)),
          winArm64UnwindEntries_(std::move(other.winArm64UnwindEntries_)),
          offsetBias_(other.offsetBias_),
          sectionIdentityAliases_(std::move(other.sectionIdentityAliases_)),
          sectionIdentity_(other.sectionIdentity_) {
        other.offsetBias_ = 0;
        other.sectionIdentityAliases_.clear();
        other.sectionIdentity_ = allocateSectionIdentity();
    }

    CodeSection &operator=(const CodeSection &other) {
        if (this == &other)
            return *this;
        bytes_ = other.bytes_;
        relocations_ = other.relocations_;
        symbols_ = other.symbols_;
        unwindEntries_ = other.unwindEntries_;
        win64UnwindEntries_ = other.win64UnwindEntries_;
        winArm64UnwindEntries_ = other.winArm64UnwindEntries_;
        offsetBias_ = other.offsetBias_;
        sectionIdentityAliases_ = other.sectionIdentityAliases_;
        sectionIdentity_ = allocateSectionIdentity();
        addSectionIdentityAlias(other.sectionIdentity_);
        retargetSelfIdentity(other.sectionIdentity_, sectionIdentity_);
        return *this;
    }

    CodeSection &operator=(CodeSection &&other) {
        if (this == &other)
            return *this;
        bytes_ = std::move(other.bytes_);
        relocations_ = std::move(other.relocations_);
        symbols_ = std::move(other.symbols_);
        unwindEntries_ = std::move(other.unwindEntries_);
        win64UnwindEntries_ = std::move(other.win64UnwindEntries_);
        winArm64UnwindEntries_ = std::move(other.winArm64UnwindEntries_);
        offsetBias_ = other.offsetBias_;
        sectionIdentityAliases_ = std::move(other.sectionIdentityAliases_);
        sectionIdentity_ = other.sectionIdentity_;
        other.offsetBias_ = 0;
        other.sectionIdentityAliases_.clear();
        other.sectionIdentity_ = allocateSectionIdentity();
        return *this;
    }

    // === Byte emission ===

    /// Reserve total byte capacity ahead of time.
    void reserveBytes(size_t totalBytes) {
        if (bytes_.capacity() < totalBytes)
            bytes_.reserve(totalBytes);
    }

    /// Reserve additional byte capacity relative to the current size.
    void reserveAdditionalBytes(size_t additionalBytes) {
        if (additionalBytes > std::numeric_limits<size_t>::max() - bytes_.size())
            throw std::length_error("CodeSection byte reservation exceeds addressable size");
        reserveBytes(bytes_.size() + additionalBytes);
    }

    /// Set a logical offset bias for dry-run encoding.
    ///
    /// This lets instruction-size estimators measure code at a non-zero logical
    /// offset without materializing padding bytes. Real object emission should
    /// leave the bias at zero.
    void setLogicalOffsetBias(size_t offsetBias) {
        if (!bytes_.empty() || !relocations_.empty() || symbols_.count() > 1)
            throw std::logic_error("CodeSection logical offset bias must be set before emission");
        offsetBias_ = offsetBias;
    }

    /// Reserve total relocation capacity ahead of time.
    void reserveRelocations(size_t totalRelocs) {
        if (relocations_.capacity() < totalRelocs)
            relocations_.reserve(totalRelocs);
    }

    /// Reserve total symbol capacity ahead of time.
    [[maybe_unused]] void reserveSymbols(size_t totalSymbols) {
        symbols_.reserve(totalSymbols);
    }

    /// Current write position (byte offset from start of section).
    size_t currentOffset() const {
        if (offsetBias_ > std::numeric_limits<size_t>::max() - bytes_.size())
            throw std::length_error("CodeSection current offset exceeds addressable size");
        return offsetBias_ + bytes_.size();
    }

    /// Append a single byte.
    void emit8(uint8_t val) {
        bytes_.push_back(val);
    }

    /// Append 2 bytes, little-endian.
    [[maybe_unused]] void emit16LE(uint16_t val) {
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
        if (len == 0)
            return;
        if (data == nullptr)
            throw std::invalid_argument("CodeSection emitBytes requires non-null data");
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
        if (alignment <= 1)
            return;
        if ((alignment & (alignment - 1)) != 0)
            throw std::invalid_argument("CodeSection alignment must be a power of two");
        const size_t offset = currentOffset();
        const size_t rem = offset % alignment;
        if (rem == 0)
            return;
        const size_t padding = alignment - rem;
        if (padding > std::numeric_limits<size_t>::max() - offset)
            throw std::length_error("CodeSection alignment exceeds addressable size");
        emitZeros(padding);
    }

    // === Relocation tracking ===

    /// Record a relocation at the current offset.
    [[maybe_unused]] void addRelocation(
        RelocKind kind,
        uint32_t symbolIndex,
        int64_t addend = 0,
        SymbolSection targetSection = SymbolSection::Undefined) {
        if (symbolIndex >= symbols_.count())
            throw std::out_of_range("CodeSection relocation symbol index is out of range");
        validateRelocationSourceRange(currentOffset(), kind);
        relocations_.push_back(
            Relocation{currentOffset(), kind, symbolIndex, addend, targetSection});
    }

    /// Record a relocation at a specific offset.
    void addRelocationAt(size_t offset,
                         RelocKind kind,
                         uint32_t symbolIndex,
                         int64_t addend = 0,
                         SymbolSection targetSection = SymbolSection::Undefined) {
        if (offset < offsetBias_ || offset - offsetBias_ > bytes_.size())
            throw std::out_of_range("CodeSection relocation offset is out of range");
        if (symbolIndex >= symbols_.count())
            throw std::out_of_range("CodeSection relocation symbol index is out of range");
        validateRelocationSourceRange(offset, kind);
        relocations_.push_back(Relocation{offset, kind, symbolIndex, addend, targetSection});
    }

    /// Record a relocation that targets a concrete offset in another section.
    ///
    /// Object writers serialize this through a section-anchor symbol plus addend,
    /// which avoids ambiguous name lookup for duplicate local labels.
    void addSectionOffsetRelocation(RelocKind kind,
                                    SymbolSection targetSection,
                                    size_t targetOffset,
                                    int64_t addend = 0) {
        if (targetSection == SymbolSection::Undefined)
            throw std::invalid_argument(
                "CodeSection section-offset relocation requires a target section");
        validateRelocationSourceRange(currentOffset(), kind);
        Relocation rel{currentOffset(), kind, 0, addend, targetSection};
        rel.targetOffsetValid = true;
        rel.targetOffset = targetOffset;
        relocations_.push_back(rel);
    }

    /// Record a relocation that targets a concrete offset in a specific CodeSection.
    void addSectionOffsetRelocation(RelocKind kind,
                                    const CodeSection &target,
                                    SymbolSection targetSection,
                                    size_t targetOffset,
                                    int64_t addend = 0) {
        if (targetSection == SymbolSection::Undefined)
            throw std::invalid_argument(
                "CodeSection section-offset relocation requires a target section");
        if (!target.containsOffsetRange(targetOffset, 0))
            throw std::out_of_range("CodeSection relocation target offset is out of range");
        validateRelocationSourceRange(currentOffset(), kind);
        Relocation rel{currentOffset(), kind, 0, addend, targetSection};
        rel.targetOffsetValid = true;
        rel.targetOffset = targetOffset;
        rel.targetSectionIdentityValid = true;
        rel.targetSectionIdentity = target.sectionIdentity();
        relocations_.push_back(rel);
    }

    /// Record a section-offset relocation at a specific source offset.
    void addSectionOffsetRelocationAt(size_t offset,
                                      RelocKind kind,
                                      SymbolSection targetSection,
                                      size_t targetOffset,
                                      int64_t addend = 0) {
        if (targetSection == SymbolSection::Undefined)
            throw std::invalid_argument(
                "CodeSection section-offset relocation requires a target section");
        if (offset < offsetBias_ || offset - offsetBias_ > bytes_.size())
            throw std::out_of_range("CodeSection relocation offset is out of range");
        validateRelocationSourceRange(offset, kind);
        Relocation rel{offset, kind, 0, addend, targetSection};
        rel.targetOffsetValid = true;
        rel.targetOffset = targetOffset;
        relocations_.push_back(rel);
    }

    /// Record a section-offset relocation at a specific source offset, with exact target identity.
    void addSectionOffsetRelocationAt(size_t offset,
                                      RelocKind kind,
                                      const CodeSection &target,
                                      SymbolSection targetSection,
                                      size_t targetOffset,
                                      int64_t addend = 0) {
        if (targetSection == SymbolSection::Undefined)
            throw std::invalid_argument(
                "CodeSection section-offset relocation requires a target section");
        if (offset < offsetBias_ || offset - offsetBias_ > bytes_.size())
            throw std::out_of_range("CodeSection relocation offset is out of range");
        if (!target.containsOffsetRange(targetOffset, 0))
            throw std::out_of_range("CodeSection relocation target offset is out of range");
        validateRelocationSourceRange(offset, kind);
        Relocation rel{offset, kind, 0, addend, targetSection};
        rel.targetOffsetValid = true;
        rel.targetOffset = targetOffset;
        rel.targetSectionIdentityValid = true;
        rel.targetSectionIdentity = target.sectionIdentity();
        relocations_.push_back(rel);
    }

    // === Symbol management ===

    /// Define a symbol at the current offset. Returns its index in the symbol table.
    uint32_t defineSymbol(const std::string &name, SymbolBinding binding, SymbolSection section) {
        if (binding == SymbolBinding::External)
            throw std::invalid_argument("CodeSection defineSymbol cannot define External symbols");
        if (section == SymbolSection::Undefined)
            throw std::invalid_argument("CodeSection defineSymbol requires a concrete section");
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
        if (!containsOffsetRange(offset, 4))
            throw std::out_of_range("CodeSection patch32LE offset is out of range");
        const size_t physicalOffset = offset - offsetBias_;
        bytes_[physicalOffset] = static_cast<uint8_t>(val);
        bytes_[physicalOffset + 1] = static_cast<uint8_t>(val >> 8);
        bytes_[physicalOffset + 2] = static_cast<uint8_t>(val >> 16);
        bytes_[physicalOffset + 3] = static_cast<uint8_t>(val >> 24);
    }

    /// Overwrite 1 byte at the given offset.
    [[maybe_unused]] void patch8(size_t offset, uint8_t val) {
        if (!containsOffsetRange(offset, 1))
            throw std::out_of_range("CodeSection patch8 offset is out of range");
        bytes_[offset - offsetBias_] = val;
    }

    /// @brief Replace an existing logical byte range without resizing the section.
    /// @details This constrained patch API is intended for writer-side addend
    ///          embedding and late fixups that already know their exact byte
    ///          payload. It preserves relocation and symbol offsets by rejecting
    ///          any range that would extend beyond emitted bytes.
    /// @param offset Logical section offset at which @p data should be written.
    /// @param data Replacement bytes. Empty ranges are accepted as no-ops.
    [[maybe_unused]] void patchBytes(size_t offset, const std::vector<uint8_t> &data) {
        if (data.empty())
            return;
        if (!containsOffsetRange(offset, data.size()))
            throw std::out_of_range("CodeSection patchBytes range is out of range");
        const size_t physicalOffset = offset - offsetBias_;
        std::copy(
            data.begin(), data.end(), bytes_.begin() + static_cast<std::ptrdiff_t>(physicalOffset));
    }

    /// Read 4 bytes at a logical offset, little-endian.
    uint32_t read32LE(size_t offset) const {
        if (!containsOffsetRange(offset, 4))
            throw std::out_of_range("CodeSection read32LE offset is out of range");
        const size_t physicalOffset = offset - offsetBias_;
        return static_cast<uint32_t>(bytes_[physicalOffset]) |
               (static_cast<uint32_t>(bytes_[physicalOffset + 1]) << 8) |
               (static_cast<uint32_t>(bytes_[physicalOffset + 2]) << 16) |
               (static_cast<uint32_t>(bytes_[physicalOffset + 3]) << 24);
    }

    // === Accessors ===

    /// Raw byte buffer.
    const std::vector<uint8_t> &bytes() const {
        return bytes_;
    }

    /// Mutable byte buffer compatibility escape hatch.
    ///
    /// Prefer patch32LE(), patch8(), or patchBytes() for bounded updates that
    /// cannot accidentally resize the section and invalidate relocation sites.
    std::vector<uint8_t> &mutableBytes() {
        return bytes_;
    }

    /// Logical offset bias added to physical byte indexes.
    size_t logicalOffsetBias() const {
        return offsetBias_;
    }

    /// Stable logical section identity for section-offset relocations.
    uint64_t sectionIdentity() const {
        return sectionIdentity_;
    }

    /// Return true when this section is, or was copied from, the requested identity.
    bool matchesSectionIdentity(uint64_t identity) const {
        if (identity == sectionIdentity_)
            return true;
        return std::find(sectionIdentityAliases_.begin(),
                         sectionIdentityAliases_.end(),
                         identity) != sectionIdentityAliases_.end();
    }

    /// Return true when [offset, offset + width) is within emitted bytes.
    bool containsOffsetRange(size_t offset, size_t width) const {
        if (offset < offsetBias_)
            return false;
        const size_t physicalOffset = offset - offsetBias_;
        return physicalOffset <= bytes_.size() && width <= bytes_.size() - physicalOffset;
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
        addSectionIdentityAlias(other.sectionIdentity());
        for (uint64_t alias : other.sectionIdentityAliases_)
            addSectionIdentityAlias(alias);
        reserveAdditionalBytes(other.bytes().size());
        if (other.relocations().size() > std::numeric_limits<size_t>::max() - relocations_.size())
            throw std::length_error("CodeSection relocation reservation exceeds addressable size");
        reserveRelocations(relocations_.size() + other.relocations().size());
        std::vector<uint32_t> symbolRemap(other.symbols().count(), 0);
        auto rebaseLogicalOffset = [&](size_t logicalOffset) -> size_t {
            if (logicalOffset < other.offsetBias_)
                throw std::out_of_range("CodeSection append source offset is before logical bias");
            const size_t physicalOffset = logicalOffset - other.offsetBias_;
            if (physicalOffset > other.bytes().size())
                throw std::out_of_range("CodeSection append source offset is out of range");
            if (physicalOffset > std::numeric_limits<size_t>::max() - offsetBias)
                throw std::length_error("CodeSection append offset exceeds addressable size");
            return offsetBias + physicalOffset;
        };

        for (uint32_t i = 1; i < other.symbols().count(); ++i) {
            const Symbol &sym = other.symbols().at(i);
            uint32_t mappedIndex = 0;

            if (sym.binding == SymbolBinding::External || sym.section == SymbolSection::Undefined) {
                mappedIndex = findOrDeclareSymbol(sym.name);
            } else {
                const size_t rebasedOffset = rebaseLogicalOffset(sym.offset);
                mappedIndex = symbols_.add(
                    Symbol{sym.name, sym.binding, sym.section, rebasedOffset, sym.size});
                Symbol &dst = symbols_.at(mappedIndex);
                dst.binding = sym.binding;
                dst.section = sym.section;
                dst.offset = rebasedOffset;
                dst.size = sym.size;
            }

            symbolRemap[i] = mappedIndex;
        }

        if (!other.bytes().empty())
            emitBytes(other.bytes().data(), other.bytes().size());

        for (const auto &reloc : other.relocations()) {
            if (reloc.symbolIndex >= symbolRemap.size())
                throw std::out_of_range(
                    "CodeSection append relocation symbol index is out of range");
            Relocation rebased = reloc;
            rebased.offset = rebaseLogicalOffset(reloc.offset);
            rebased.symbolIndex = symbolRemap[reloc.symbolIndex];
            const bool targetsAppendedIdentity =
                rebased.targetSectionIdentityValid &&
                other.matchesSectionIdentity(rebased.targetSectionIdentity);
            if (rebased.targetOffsetValid && !rebased.targetSectionIdentityValid) {
                throw std::logic_error(
                    "CodeSection append requires section identity for section-offset relocation");
            }
            if (rebased.targetOffsetValid && targetsAppendedIdentity) {
                rebased.targetOffset = rebaseLogicalOffset(reloc.targetOffset);
                rebased.targetSectionIdentityValid = true;
                rebased.targetSectionIdentity = sectionIdentity_;
            }
            relocations_.push_back(rebased);
        }

        for (const auto &entry : other.unwindEntries()) {
            if (entry.symbolIndex >= symbolRemap.size())
                throw std::out_of_range(
                    "CodeSection append compact unwind symbol index is out of range");
            CompactUnwindEntry rebased = entry;
            rebased.symbolIndex = symbolRemap[entry.symbolIndex];
            unwindEntries_.push_back(rebased);
        }

        for (const auto &entry : other.win64UnwindEntries()) {
            if (entry.symbolIndex >= symbolRemap.size())
                throw std::out_of_range(
                    "CodeSection append Win64 unwind symbol index is out of range");
            Win64UnwindEntry rebased = entry;
            rebased.symbolIndex = symbolRemap[entry.symbolIndex];
            win64UnwindEntries_.push_back(std::move(rebased));
        }

        for (const auto &entry : other.winArm64UnwindEntries()) {
            if (entry.symbolIndex >= symbolRemap.size())
                throw std::out_of_range(
                    "CodeSection append Windows ARM64 unwind symbol index is out of range");
            WinArm64UnwindEntry rebased = entry;
            rebased.symbolIndex = symbolRemap[entry.symbolIndex];
            winArm64UnwindEntries_.push_back(std::move(rebased));
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

    /// Record a Windows ARM64 unwind entry for a function.
    void addWinArm64UnwindEntry(WinArm64UnwindEntry entry) {
        winArm64UnwindEntries_.push_back(std::move(entry));
    }

    /// All recorded Windows ARM64 unwind entries.
    const std::vector<WinArm64UnwindEntry> &winArm64UnwindEntries() const {
        return winArm64UnwindEntries_;
    }

  private:
    void addSectionIdentityAlias(uint64_t identity) {
        if (identity == 0 || identity == sectionIdentity_)
            return;
        if (std::find(sectionIdentityAliases_.begin(), sectionIdentityAliases_.end(), identity) ==
            sectionIdentityAliases_.end())
            sectionIdentityAliases_.push_back(identity);
    }

    void retargetSelfIdentity(uint64_t oldIdentity, uint64_t newIdentity) {
        for (auto &rel : relocations_) {
            if (rel.targetSectionIdentityValid && rel.targetSectionIdentity == oldIdentity)
                rel.targetSectionIdentity = newIdentity;
        }
    }

    /// @brief Validate that a relocation source offset is within this section.
    /// @details Relocation offsets are recorded as logical section offsets, which
    ///          may include an offset bias during dry-run encoding. Relocations
    ///          are often registered before the placeholder bytes are emitted, so
    ///          this check intentionally accepts the current end offset and leaves
    ///          architecture-specific fixup-width validation to object writers.
    /// @param offset Logical section offset of the fixup field.
    /// @param kind Relocation kind; retained for call-site clarity and future diagnostics.
    /// @throws std::out_of_range if the source offset is outside emitted bytes.
    void validateRelocationSourceRange(size_t offset, RelocKind kind) const {
        (void)kind;
        if (offset < offsetBias_ || offset - offsetBias_ > bytes_.size())
            throw std::out_of_range("CodeSection relocation source range is out of bounds");
    }

    static uint64_t allocateSectionIdentity() {
        const uint64_t id = nextSectionIdentity_.fetch_add(1, std::memory_order_relaxed);
        if (id == 0)
            throw std::length_error("CodeSection identity counter wrapped");
        return id;
    }

    std::vector<uint8_t> bytes_;
    std::vector<Relocation> relocations_;
    SymbolTable symbols_;
    std::vector<CompactUnwindEntry> unwindEntries_;
    std::vector<Win64UnwindEntry> win64UnwindEntries_;
    std::vector<WinArm64UnwindEntry> winArm64UnwindEntries_;
    size_t offsetBias_ = 0;
    std::vector<uint64_t> sectionIdentityAliases_;
    uint64_t sectionIdentity_ = 0;
    inline static std::atomic<uint64_t> nextSectionIdentity_{1};
};

} // namespace viper::codegen::objfile
