//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/LinkTypes.hpp
// Purpose: Core types used throughout the native linker: OutputSection,
//          InputChunk, LinkLayout, GlobalSymEntry, and platform enums.
// Key invariants:
//   - OutputSection owns concatenated section data + reloc list
//   - Virtual addresses assigned during layout phase
//   - Page alignment differs per platform: macOS arm64=16KB, others=4KB
// Ownership/Lifetime:
//   - All types are value types owned by NativeLinker
// Links: codegen/common/linker/ObjFileReader.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper::codegen::linker {

/// Target platform for the linker.
enum class LinkPlatform : uint8_t {
    Linux,
    macOS,
    Windows,
};

/// Target architecture for the linker.
enum class LinkArch : uint8_t {
    X86_64,
    AArch64,
};

/// Detect host link platform.
constexpr LinkPlatform detectLinkPlatform() {
#if defined(__APPLE__)
    return LinkPlatform::macOS;
#elif defined(_WIN32)
    return LinkPlatform::Windows;
#else
    return LinkPlatform::Linux;
#endif
}

/// Detect host link architecture.
constexpr LinkArch detectLinkArch() {
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return LinkArch::AArch64;
#else
    return LinkArch::X86_64;
#endif
}

/// Conventional image base address used by the native linker for each platform.
constexpr uint64_t defaultImageBaseForPlatform(LinkPlatform platform) {
    switch (platform) {
        case LinkPlatform::macOS:
            return 0x100000000ULL;
        case LinkPlatform::Windows:
            return 0x140000000ULL;
        case LinkPlatform::Linux:
        default:
            return 0x400000ULL;
    }
}

/// A chunk of data from one input section within an output section.
struct InputChunk {
    size_t inputObjIndex; ///< Index into the linker's object file list.
    size_t inputSecIndex; ///< Index into that ObjFile's sections.
    size_t outputOffset;  ///< Byte offset within the output section.
    size_t size;          ///< Size in bytes.
    bool synthetic = false; ///< True when the bytes were created by the linker.
};

/// Hashable key for maps indexed by an input object/section pair.
struct InputSectionKey {
    size_t objIndex = 0;
    size_t secIndex = 0;

    bool operator==(const InputSectionKey &other) const noexcept {
        return objIndex == other.objIndex && secIndex == other.secIndex;
    }
};

struct InputSectionKeyHash {
    size_t operator()(const InputSectionKey &key) const noexcept {
        size_t h = std::hash<size_t>{}(key.objIndex);
        h ^= std::hash<size_t>{}(key.secIndex) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

/// A merged output section.
struct OutputSection {
    std::string name;
    std::vector<uint8_t> data;      ///< Concatenated section bytes.
    size_t memSize = 0;             ///< Logical in-memory size, including zero-fill bytes.
    std::vector<InputChunk> chunks; ///< Provenance info for each chunk.
    uint64_t virtualAddr = 0;       ///< Virtual address after layout.
    uint32_t alignment = 1;         ///< Required alignment.
    bool executable = false;
    bool writable = false;
    bool tls = false;
    bool zeroFill = false; ///< Occupies memory but has no file backing.
    bool alloc = true; ///< Section is loadable (false for debug sections).
    bool dataSegment = false; ///< Emit in data segment even when final protections are read-only.
};

inline size_t outputSectionMemSize(const OutputSection &sec) {
    return sec.zeroFill ? (sec.memSize != 0 ? sec.memSize : sec.data.size()) : sec.data.size();
}

/// Section classification for merging.
enum class SectionClass : uint8_t {
    Text,    ///< Executable code.
    Rodata,  ///< Read-only data.
    Data,    ///< Read-write data.
    Bss,     ///< Uninitialized data (zero-filled).
    TlsData, ///< Thread-local initialized data.
    TlsBss,  ///< Thread-local uninitialized data.
    ObjC,    ///< ObjC metadata — preserved with original section name.
    Preserved, ///< Platform metadata preserved with its original section name.
    Other,   ///< Non-allocatable, debug, etc.
};

/// Check whether a Mach-O section name is ObjC metadata that must be preserved.
/// The ObjC runtime locates classes, selectors, protocols, etc. by section name.
inline bool isObjCSection(const std::string &name) {
    return name.find("__objc_") != std::string::npos;
}

/// Check whether a Windows PE/COFF metadata section name must be preserved.
/// The PE loader and unwinder expect these sections to remain separately
/// addressable so the exe writer can publish the matching data directories.
inline bool isWindowsMetadataSection(const std::string &name) {
    return name.rfind(".pdata", 0) == 0 || name.rfind(".xdata", 0) == 0;
}

inline bool isElfMetadataSection(const std::string &name) {
    return name == ".eh_frame" || name.rfind(".eh_frame.", 0) == 0 ||
           name == ".gcc_except_table" || name.rfind(".gcc_except_table.", 0) == 0 ||
           name.rfind(".note.", 0) == 0 || name == ".note";
}

inline bool isMachOConstDataSection(const std::string &name) {
    return name.rfind("__DATA_CONST,", 0) == 0 || name.rfind("__AUTH_CONST,", 0) == 0;
}

inline bool isPreservedNamedSection(const std::string &name) {
    return isObjCSection(name) || isWindowsMetadataSection(name) || isElfMetadataSection(name) ||
           isMachOConstDataSection(name);
}

/// Symbols synthesized by the Windows native linker rather than imported from
/// a DLL or provided by a runtime archive.
inline bool isWindowsStdioOptionsStorageSymbol(const std::string &name) {
    return name == "?_OptionsStorage@?1??__local_stdio_printf_options@@9@9" ||
           name == "?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9";
}

inline bool isWindowsLinkerHelperSymbol(const std::string &name) {
    return name == "_fltused" || name == "__ImageBase" ||
           name == "__security_cookie" || name == "__security_check_cookie" ||
           name == "__security_init_cookie" || name == "__GSHandlerCheck" ||
           name == "__GSHandlerCheck_EH4" || name == "_RTC_InitBase" || name == "_RTC_Shutdown" ||
           name == "_RTC_CheckStackVars" || name == "_RTC_UninitUse" ||
           name == "__report_rangecheckfailure" || name == "__chkstk" || name == "_tls_index" ||
           name == "__security_cookie_complement" || name == "__guard_dispatch_icall_fptr" ||
           name == "_is_c_termination_complete" || name == "__vcrt_initialize" ||
           name == "__vcrt_thread_attach" || name == "__vcrt_thread_detach" ||
           name == "__vcrt_uninitialize" || name == "__vcrt_uninitialize_critical" ||
           name == "__acrt_initialize" || name == "__acrt_thread_attach" ||
           name == "__acrt_thread_detach" || name == "__acrt_uninitialize" ||
           name == "__acrt_uninitialize_critical" || name == "__isa_available_init" ||
           name == "__scrt_exe_initialize_mta" ||
           name == "__CxxFrameHandler4" || name == "??_7type_info@@6B@" ||
           name == "??2@YAPEAX_K@Z" ||
           name == "??2@YAPEAX_KAEBUnothrow_t@std@@@Z" ||
           name == "??3@YAXPEAX@Z" || name == "??3@YAXPEAX_K@Z" ||
           name == "IID_ID3D11Texture2D" ||
           isWindowsStdioOptionsStorageSymbol(name) || name == "vm_trap" ||
           name == "rt_audio_shutdown";
}

/// Classify a section by name and attributes.
inline SectionClass classifySection(const std::string &name,
                                    bool executable,
                                    bool writable,
                                    bool tls,
                                    bool zeroFill) {
    if (tls) {
        if (zeroFill || name.find("bss") != std::string::npos ||
            name.find("zerofill") != std::string::npos)
            return SectionClass::TlsBss;
        return SectionClass::TlsData;
    }
    if (isObjCSection(name))
        return SectionClass::ObjC;
    // Platform metadata must be preserved with its original name because
    // runtimes/loaders locate it by name or dedicated data-directory ranges.
    if (isWindowsMetadataSection(name) || isElfMetadataSection(name) ||
        isMachOConstDataSection(name))
        return SectionClass::Preserved;
    if (executable)
        return SectionClass::Text;
    if (writable) {
        if (zeroFill || name.find("bss") != std::string::npos ||
            name.find("UNINITIALIZED") != std::string::npos)
            return SectionClass::Bss;
        return SectionClass::Data;
    }
    // Read-only: only known text-section spellings are code when producer flags
    // failed to mark them executable. Do not classify arbitrary names containing
    // ".text" as executable code.
    if (name == ".text" || name.rfind(".text.", 0) == 0 || name.rfind(".text$", 0) == 0 ||
        name == "__TEXT,__text")
        return SectionClass::Text;
    return SectionClass::Rodata;
}

/// Global symbol table entry.
struct GlobalSymEntry {
    std::string name;

    enum Binding : uint8_t {
        Undefined,
        Global,
        Weak,
        Dynamic, ///< Provided by a shared library.
    } binding = Undefined;

    size_t objIndex = 0;       ///< Which ObjFile defined this symbol.
    uint32_t secIndex = 0;     ///< Section within that ObjFile.
    size_t offset = 0;         ///< Offset within the section.
    uint64_t resolvedAddr = 0; ///< Final virtual address after layout.
    bool resolvedAddrValid = false; ///< True when resolvedAddr is an intentional address, including zero.
    bool absolute = false;     ///< Symbol resolves to offset/resolvedAddr directly.
    bool common = false;       ///< Tentative/common symbol awaiting materialization.
    size_t commonSize = 0;     ///< Largest requested tentative definition size.
    size_t commonAlignment = 1; ///< Maximum requested tentative definition alignment.
};

/// A GOT entry for dynamic symbol binding.
struct GotEntry {
    std::string symbolName; ///< External symbol name (e.g., "printf").
    uint64_t gotAddr = 0;   ///< Virtual address of this GOT slot.
};

/// A location in the output that contains an absolute pointer needing ASLR rebase.
struct RebaseEntry {
    size_t sectionIndex; ///< Index into LinkLayout::sections.
    size_t offset;       ///< Byte offset within the output section.
};

/// A data-pointer location that must be bound to a dynamic symbol at load time.
/// Used for non-GOT references (e.g., ObjC classrefs) to external symbols.
struct BindEntry {
    std::string symbolName; ///< External symbol name (e.g., "OBJC_CLASS_$_NSColor").
    size_t sectionIndex;    ///< Index into LinkLayout::sections.
    size_t offset;          ///< Byte offset within the output section.
};

/// Complete memory layout for the linked output.
struct LinkLayout {
    std::vector<OutputSection> sections;                        ///< Merged output sections.
    std::unordered_map<std::string, GlobalSymEntry> globalSyms; ///< All resolved symbols.
    uint64_t entryAddr = 0;                                     ///< Entry point virtual address.
    size_t pageSize = 0x1000;                                   ///< Page size (platform-dependent).
    std::vector<GotEntry> gotEntries;       ///< GOT entries for dynamic linking.
    std::vector<RebaseEntry> rebaseEntries; ///< Locations needing ASLR pointer rebase.
    std::vector<BindEntry> bindEntries;     ///< Non-GOT data pointers needing dyld bind.
};

} // namespace viper::codegen::linker
