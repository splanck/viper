//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/ObjFileReader.hpp
// Purpose: Unified object file representation and reader interface for the
//          native linker. Provides ObjFile/ObjSection/ObjSymbol/ObjReloc
//          types and format-specific readers for ELF, Mach-O, and COFF.
// Key invariants:
//   - Auto-detects format from magic bytes
//   - Symbol names are unmangled (Mach-O '_' prefix stripped)
//   - Relocations carry addend uniformly (extracted from instructions for
//     Mach-O and COFF which lack explicit addend fields)
// Ownership/Lifetime:
//   - ObjFile owns all parsed data including section bytes
// Links: codegen/common/linker/ArchiveReader.hpp
//        codegen/common/objfile/Relocation.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace viper::codegen::linker {

enum class ComdatSelection : uint8_t {
    None,
    NoDuplicates,
    Any,
    SameSize,
    ExactMatch,
    Associative,
    Largest,
};

/// Relocation in a parsed object file.
struct ObjReloc {
    size_t offset = 0;            ///< Byte offset within the section.
    uint32_t type = 0;            ///< Format-native relocation type (e.g., R_X86_64_PLT32).
    uint32_t symIndex = 0;        ///< Index into ObjFile::symbols.
    int64_t addend = 0;           ///< Addend (explicit for ELF; extracted for Mach-O/COFF).
    bool pcrel = false;           ///< Mach-O r_pcrel bit; unused by ELF/COFF readers.
    uint8_t length = 0;           ///< Mach-O r_length field (0=byte, 1=word, 2=long, 3=quad).
    bool sectionRelative = false; ///< Reader-internal: raw reloc targeted a section ordinal.
};

/// Symbol in a parsed object file.
struct ObjSymbol {
    std::string name;

    enum Binding : uint8_t { Local, Global, Weak, Undefined } binding = Undefined;

    uint32_t sectionIndex = 0; ///< Index into ObjFile::sections (0 = undefined).
    size_t offset = 0;         ///< Byte offset within section.
    size_t size = 0;
    bool absolute = false;       ///< Symbol value is absolute, not section-relative.
    bool weakExternal = false;   ///< Undefined weak external that may resolve to zero/default.
    std::string weakDefaultName; ///< Optional COFF weak-external fallback symbol.
    uint32_t weakExternalCharacteristics = 0; ///< COFF IMAGE_WEAK_EXTERN_SEARCH_* value.
    bool common = false;                      ///< Tentative/common symbol, coalesced by linker.
    size_t commonAlignment = 1;               ///< Required alignment for common storage.
};

/// Section in a parsed object file.
struct ObjSection {
    std::string name;
    std::vector<uint8_t> data;
    size_t memSize = 0; ///< Logical in-memory size. For non-zero-fill sections this is data.size().
    std::vector<ObjReloc> relocs;
    uint32_t alignment = 1;
    bool executable = false;
    bool writable = false;
    bool alloc = true;               ///< Section contributes to memory image.
    bool tls = false;                ///< Thread-local storage section.
    bool zeroFill = false;           ///< Section occupies memory but has no file bytes.
    bool dataSegment = false;        ///< Must be emitted in a data segment even if final read-only.
    bool isCStringSection = false;   ///< Section contains NUL-terminated C strings only.
    uint32_t associativeSection = 0; ///< COFF associative COMDAT parent section, if any.
    ComdatSelection comdatSelection = ComdatSelection::None; ///< Duplicate-selection policy.
    std::string comdatKey; ///< COFF/ELF COMDAT group key/signature.
    bool stripped = false; ///< Dead-strip removed this section explicitly.
};

inline size_t objSectionMemSize(const ObjSection &sec) {
    return sec.zeroFill ? (sec.memSize != 0 ? sec.memSize : sec.data.size()) : sec.data.size();
}

/// Detected object file format.
enum class ObjFileFormat : uint8_t {
    ELF,
    MachO,
    COFF,
    Unknown,
};

/// Complete parsed object file.
struct ObjFile {
    std::string name; ///< Source file name or archive member name.
    ObjFileFormat format = ObjFileFormat::Unknown;
    bool is64bit = true;
    bool isLittleEndian = true;
    bool synthetic = false; ///< True for linker-created helper objects, never user/archive input.
    uint16_t machine = 0;   ///< Machine type (EM_X86_64, EM_AARCH64, etc.)

    std::vector<ObjSection> sections; ///< Index 0 is reserved (null section).
    std::vector<ObjSymbol> symbols;   ///< Index 0 is reserved (null symbol).
};

/// Maximum number of sections/symbols accepted from an object file.
/// Guards against corrupt or malicious inputs causing unbounded allocation.
inline constexpr uint32_t kMaxObjSections = 1 << 20;
inline constexpr uint32_t kMaxObjSymbols = 1 << 20; // 1M symbols.
inline constexpr size_t kMaxObjSectionBytes = 2ULL * 1024 * 1024 * 1024;
inline constexpr size_t kMaxObjMaterializedBytes = 4ULL * 1024 * 1024 * 1024;

/// Detect the object file format from magic bytes.
ObjFileFormat detectFormat(const uint8_t *data, size_t size);

/// Read an object file from raw bytes.
/// @param data  Raw bytes of the object file.
/// @param size  Size in bytes.
/// @param name  Name for diagnostics (file path or archive member name).
/// @param obj   Output parsed object file.
/// @param err   Error output stream.
/// @return true on success.
bool readObjFile(
    const uint8_t *data, size_t size, const std::string &name, ObjFile &obj, std::ostream &err);

/// Read an object file from a file path.
bool readObjFile(const std::string &path, ObjFile &obj, std::ostream &err);

// Format-specific readers (called by readObjFile after detectFormat).
// All three share the readObjFile() signature; the dispatcher picks one based
// on the magic bytes.

/// @brief Parse an ELF relocatable object (ET_REL) into @p obj.
bool readElfObj(
    const uint8_t *data, size_t size, const std::string &name, ObjFile &obj, std::ostream &err);

/// @brief Parse a Mach-O object file (MH_OBJECT) into @p obj.
bool readMachOObj(
    const uint8_t *data, size_t size, const std::string &name, ObjFile &obj, std::ostream &err);

/// @brief Parse a COFF object (Microsoft x64 / ARM64) into @p obj.
bool readCoffObj(
    const uint8_t *data, size_t size, const std::string &name, ObjFile &obj, std::ostream &err);

} // namespace viper::codegen::linker
