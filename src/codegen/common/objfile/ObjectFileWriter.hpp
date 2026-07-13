//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/ObjectFileWriter.hpp
// Purpose: Abstract interface for object file writers (ELF, Mach-O, COFF).
//          Concrete implementations serialize CodeSection data into the
//          appropriate binary format.
// Key invariants:
//   - Each writer handles format-specific relocation mapping and symbol mangling
//   - The factory function creates the writer for the target platform
// Ownership/Lifetime:
//   - Created via factory, caller owns the unique_ptr
// Links: codegen/common/objfile/CodeSection.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/objfile/CodeSection.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace viper::codegen::objfile {

/// Target architecture for the object file.
enum class ObjArch : uint8_t {
    X86_64,
    AArch64,
};

/// Target object file format.
enum class ObjFormat : uint8_t {
    ELF,   ///< Linux.
    MachO, ///< macOS.
    COFF,  ///< Windows.
};

/// Abstract base for object file writers.
class ObjectFileWriter {
  public:
    virtual ~ObjectFileWriter() = default;

    /// Set pre-encoded DWARF .debug_line section data.
    /// If non-empty, concrete writers will emit a .debug_line section.
    void setDebugLineData(std::vector<uint8_t> data) {
        debugLineData_ = std::move(data);
    }

    /// Set the writable initialized-data section (.data / __DATA,__data).
    /// If present and non-empty, concrete writers emit a writable data section whose
    /// (global) symbols satisfy text relocations to mutable scalar globals.
    void setDataSection(CodeSection data) {
        dataSection_ = std::move(data);
    }

    /// Write a complete .o file to disk.
    /// @param path   Output file path.
    /// @param text   Machine code section (.text).
    /// @param rodata Read-only data section (.rodata). May be empty.
    /// @param err    Error output stream.
    /// @return true on success, false on failure.
    virtual bool write(const std::string &path,
                       const CodeSection &text,
                       const CodeSection &rodata,
                       std::ostream &err) = 0;

    /// Write a .o file with per-function text sections.
    /// Each CodeSection in @p textSections becomes a separate `.text.funcname`
    /// section in the output, enabling per-function dead stripping at link time.
    /// Default implementation merges all text sections and calls single-section write().
    virtual bool write(const std::string &path,
                       const std::vector<CodeSection> &textSections,
                       const CodeSection &rodata,
                       std::ostream &err);

    /// @brief Serialize a single-text-section relocatable object into memory.
    /// @details Uses the same concrete ELF, Mach-O, or COFF serializer as
    ///          @ref write, but captures its completed byte buffer instead of
    ///          opening a temporary file. Writer configuration such as debug
    ///          lines and writable data sections is honored unchanged.
    /// @param output Receives the complete relocatable object bytes on success.
    /// @param text Machine-code text section.
    /// @param rodata Read-only data section, which may be empty.
    /// @param err Diagnostic stream for serialization failures.
    /// @return True when serialization completed and @p output is valid.
    bool writeToMemory(std::vector<uint8_t> &output,
                       const CodeSection &text,
                       const CodeSection &rodata,
                       std::ostream &err);

    /// @brief Serialize per-function text sections into an in-memory object.
    /// @details Preserves the concrete writer's per-function section behavior,
    ///          enabling the native linker to dead-strip functions without an
    ///          intermediate filesystem write/read cycle.
    /// @param output Receives the complete relocatable object bytes on success.
    /// @param textSections Ordered function text sections.
    /// @param rodata Read-only data section, which may be empty.
    /// @param err Diagnostic stream for serialization failures.
    /// @return True when serialization completed and @p output is valid.
    bool writeToMemory(std::vector<uint8_t> &output,
                       const std::vector<CodeSection> &textSections,
                       const CodeSection &rodata,
                       std::ostream &err);

  protected:
    /// @brief Commit a concrete writer's completed object bytes.
    /// @details Captures bytes for an active @ref writeToMemory call; otherwise
    ///          writes them through the ordinary checked file stream path.
    /// @param path Destination path used for filesystem output and diagnostics.
    /// @param bytes Fully serialized object bytes.
    /// @param writerName Concrete writer name used in diagnostics.
    /// @param err Diagnostic stream for open/write failures.
    /// @return True when the memory capture or complete file write succeeds.
    bool commitOutput(const std::string &path,
                      const std::vector<uint8_t> &bytes,
                      const char *writerName,
                      std::ostream &err);

    std::vector<uint8_t> debugLineData_;     ///< Pre-encoded DWARF .debug_line bytes.
    std::optional<CodeSection> dataSection_; ///< Writable initialized-data section (if any).

  private:
    std::vector<uint8_t> *memoryOutput_ = nullptr; ///< Active in-memory serialization sink.
};

/// Detect the host object file format at compile time.
constexpr ObjFormat detectHostFormat() {
#if defined(__APPLE__)
    return ObjFormat::MachO;
#elif defined(_WIN32)
    return ObjFormat::COFF;
#else
    return ObjFormat::ELF;
#endif
}

/// Detect the host architecture at compile time.
constexpr ObjArch detectHostArch() {
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return ObjArch::AArch64;
#else
    return ObjArch::X86_64;
#endif
}

/// Factory: create the appropriate writer for the given format and architecture.
std::unique_ptr<ObjectFileWriter> createObjectFileWriter(ObjFormat format, ObjArch arch);

/// Factory: create a writer for the host platform.
inline std::unique_ptr<ObjectFileWriter> createHostObjectFileWriter() {
    return createObjectFileWriter(detectHostFormat(), detectHostArch());
}

} // namespace viper::codegen::objfile
