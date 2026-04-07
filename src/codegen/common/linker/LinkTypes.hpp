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

/// A chunk of data from one input section within an output section.
struct InputChunk {
    size_t inputObjIndex; ///< Index into the linker's object file list.
    size_t inputSecIndex; ///< Index into that ObjFile's sections.
    size_t outputOffset;  ///< Byte offset within the output section.
    size_t size;          ///< Size in bytes.
};

/// A merged output section.
struct OutputSection {
    std::string name;
    std::vector<uint8_t> data;      ///< Concatenated section bytes.
    std::vector<InputChunk> chunks; ///< Provenance info for each chunk.
    uint64_t virtualAddr = 0;       ///< Virtual address after layout.
    uint32_t alignment = 1;         ///< Required alignment.
    bool executable = false;
    bool writable = false;
    bool tls = false;
    bool zeroFill = false; ///< Occupies memory but has no file backing.
    bool alloc = true; ///< Section is loadable (false for debug sections).
};

/// Section classification for merging.
enum class SectionClass : uint8_t {
    Text,    ///< Executable code.
    Rodata,  ///< Read-only data.
    Data,    ///< Read-write data.
    Bss,     ///< Uninitialized data (zero-filled).
    TlsData, ///< Thread-local initialized data.
    TlsBss,  ///< Thread-local uninitialized data.
    ObjC,    ///< ObjC metadata — preserved with original section name.
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

/// Symbols synthesized by the Windows native linker rather than imported from
/// a DLL or provided by a runtime archive.
inline bool isWindowsLinkerHelperSymbol(const std::string &name) {
    return name == "_fltused" || name == "__security_cookie" || name == "__security_check_cookie" ||
           name == "__security_init_cookie" || name == "__GSHandlerCheck" ||
           name == "_RTC_InitBase" || name == "_RTC_Shutdown" || name == "_RTC_CheckStackVars" ||
           name == "__report_rangecheckfailure" || name == "__chkstk" || name == "_tls_index" ||
           name == "__security_cookie_complement" || name == "__guard_dispatch_icall_fptr" ||
           name == "_is_c_termination_complete" || name == "__vcrt_initialize" ||
           name == "__vcrt_thread_attach" || name == "__vcrt_thread_detach" ||
           name == "__vcrt_uninitialize" || name == "__vcrt_uninitialize_critical" ||
           name == "__acrt_initialize" || name == "__acrt_thread_attach" ||
           name == "__acrt_thread_detach" || name == "__acrt_uninitialize" ||
           name == "__acrt_uninitialize_critical" || name == "__isa_available_init" ||
           name == "__scrt_exe_initialize_mta" ||
           name == "?_OptionsStorage@?1??__local_stdio_printf_options@@9@9" ||
           name == "?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9" || name == "vm_trap" ||
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
    // ObjC metadata and PE unwind sections must be preserved with their
    // original names because downstream runtimes/loaders locate them by name
    // or by dedicated data-directory ranges derived from those names.
    if (isObjCSection(name) || isWindowsMetadataSection(name))
        return SectionClass::ObjC;
    if (executable)
        return SectionClass::Text;
    if (writable) {
        if (zeroFill || name.find("bss") != std::string::npos ||
            name.find("UNINITIALIZED") != std::string::npos)
            return SectionClass::Bss;
        return SectionClass::Data;
    }
    // Read-only: could be .rodata, .rdata, __TEXT,__const, etc.
    if (name.find(".text") != std::string::npos || name.find("__text") != std::string::npos)
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
