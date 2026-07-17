//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/ExeWriterUtil.hpp
// Purpose: Shared utilities for executable file writers (ELF, Mach-O, PE).
//          Provides endianness encoding, padding, and entry point resolution.
// Key invariants:
//   - All encoding functions append to a vector<uint8_t> buffer
//   - Little-endian: host byte order on supported platforms (x86-64, AArch64)
//   - Big-endian: used for Mach-O code signature (network byte order)
//   - ULEB128: unsigned LEB128 encoding for Mach-O bind/rebase opcodes
// Ownership/Lifetime:
//   - Stateless inline utilities — no allocation or side effects
// Links: codegen/common/linker/ElfExeWriter.cpp,
//        codegen/common/linker/MachOExeWriter.cpp,
//        codegen/common/linker/PeExeWriter.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/linker/LinkTypes.hpp"
#include "common/PlatformCapabilities.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace zanna::codegen::linker {

/// @brief Little-endian and big-endian encoding utilities for binary writers.
namespace encoding {

/// Append a 16-bit value in little-endian byte order.
inline void writeLE16(std::vector<uint8_t> &buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v));
    buf.push_back(static_cast<uint8_t>(v >> 8));
}

/// Append a 32-bit value in little-endian byte order.
inline void writeLE32(std::vector<uint8_t> &buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v));
    buf.push_back(static_cast<uint8_t>(v >> 8));
    buf.push_back(static_cast<uint8_t>(v >> 16));
    buf.push_back(static_cast<uint8_t>(v >> 24));
}

/// Append a 64-bit value in little-endian byte order.
inline void writeLE64(std::vector<uint8_t> &buf, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        buf.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

/// Append a 32-bit value in big-endian byte order (network byte order).
inline void writeBE32(std::vector<uint8_t> &buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v >> 24));
    buf.push_back(static_cast<uint8_t>(v >> 16));
    buf.push_back(static_cast<uint8_t>(v >> 8));
    buf.push_back(static_cast<uint8_t>(v));
}

/// Append a 64-bit value in big-endian byte order.
inline void writeBE64(std::vector<uint8_t> &buf, uint64_t v) {
    writeBE32(buf, static_cast<uint32_t>(v >> 32));
    writeBE32(buf, static_cast<uint32_t>(v));
}

/// Append an unsigned LEB128-encoded value (used by Mach-O bind/rebase opcodes).
inline void writeULEB128(std::vector<uint8_t> &buf, uint64_t val) {
    do {
        uint8_t byte = val & 0x7F;
        val >>= 7;
        if (val != 0)
            byte |= 0x80;
        buf.push_back(byte);
    } while (val != 0);
}

/// Append a signed LEB128-encoded value (used by DWARF line number deltas).
inline void writeSLEB128(std::vector<uint8_t> &buf, int64_t val) {
    bool more = true;
    while (more) {
        uint8_t byte = static_cast<uint8_t>(val) & 0x7F;
        val >>= 7;
        if ((val == 0 && (byte & 0x40) == 0) || (val == -1 && (byte & 0x40) != 0))
            more = false;
        else
            byte |= 0x80;
        buf.push_back(byte);
    }
}

/// Append \p count zero bytes.
inline void writePad(std::vector<uint8_t> &buf, size_t count) {
    buf.insert(buf.end(), count, 0);
}

/// Append a null-terminated string, padded to \p maxLen bytes.
inline void writeStr(std::vector<uint8_t> &buf, const char *s, size_t maxLen) {
    size_t len = std::strlen(s);
    if (len > maxLen)
        throw std::length_error("fixed-width binary string field overflow");
    buf.insert(buf.end(), s, s + len);
    if (len < maxLen)
        writePad(buf, maxLen - len);
}

/// Pad the buffer with zeros until it reaches \p targetSize bytes.
/// No-op if the buffer is already at or beyond the target.
inline void padTo(std::vector<uint8_t> &buf, size_t targetSize) {
    if (buf.size() < targetSize)
        buf.insert(buf.end(), targetSize - buf.size(), 0);
}

} // namespace encoding

/// Resolve the "main" or "_main" entry point symbol from the layout.
/// @return The resolved virtual address, or 0 if not found.
inline uint64_t resolveMainAddress(const LinkLayout &layout) {
    auto it = layout.globalSyms.find("main");
    if (it != layout.globalSyms.end())
        return it->second.resolvedAddr;
    it = layout.globalSyms.find("_main");
    if (it != layout.globalSyms.end())
        return it->second.resolvedAddr;
    return 0;
}

/// Partition layout section indices into text/rodata and data-segment groups.
/// Only includes allocatable sections with a non-zero memory footprint.
inline void classifySections(const LinkLayout &layout,
                             std::vector<size_t> &textIndices,
                             std::vector<size_t> &dataIndices) {
    for (size_t i = 0; i < layout.sections.size(); ++i) {
        if (outputSectionMemSize(layout.sections[i]) == 0)
            continue;
        if (!layout.sections[i].alloc)
            continue; // Skip non-alloc sections (e.g., .debug_line).
        if (layout.sections[i].writable || layout.sections[i].dataSegment ||
            layout.sections[i].zeroFill || layout.sections[i].tls)
            dataIndices.push_back(i);
        else
            textIndices.push_back(i);
    }
}

/// Compute the VA span of a set of sections within a single segment.
/// Returns the byte distance from the first section's VA to the end of the last section.
/// Handles VA gaps between sections (e.g., page-aligned subsections).
/// Returns 0 if the index list is empty.
inline size_t computeSegmentSpan(const LinkLayout &layout, const std::vector<size_t> &indices) {
    if (indices.empty())
        return 0;
    uint64_t firstVA = layout.sections[indices.front()].virtualAddr;
    size_t span = 0;
    for (size_t idx : indices) {
        const auto &sec = layout.sections[idx];
        if (sec.virtualAddr < firstVA)
            throw std::length_error("section virtual address precedes segment base");
        const size_t memSize = outputSectionMemSize(sec);
        if (memSize > std::numeric_limits<uint64_t>::max() - sec.virtualAddr)
            throw std::length_error("section virtual address range overflows");
        const uint64_t endVA = sec.virtualAddr + memSize;
        const uint64_t endOff64 = endVA - firstVA;
        if (endOff64 > std::numeric_limits<size_t>::max())
            throw std::length_error("segment span exceeds addressable size");
        size_t endOff = static_cast<size_t>(endOff64);
        if (endOff > span)
            span = endOff;
    }
    return span;
}

/// Write a binary file by creating a fresh temporary inode in the destination
/// directory and renaming it into place. This avoids in-place executable
/// mutation, which can leave macOS launch services and code-sign validation
/// observing stale vnode state for native binaries.
inline bool writeBinaryFileAtomically(const std::string &path,
                                      const std::vector<uint8_t> &data,
                                      bool makeExecutable,
                                      std::ostream &err) {
    namespace fs = std::filesystem;

    /// @brief Write the complete output buffer to @p target and apply final mode bits.
    /// @details The stream API takes a signed byte count, so this rejects files
    ///          larger than std::streamsize can represent before narrowing. On
    ///          non-Windows hosts it also reports chmod-style permission failures
    ///          instead of silently returning a non-executable binary.
    auto writeDirect = [&](const fs::path &target) -> bool {
        if (data.size() > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
            err << "error: output file '" << target.string()
                << "' exceeds stream write size limit\n";
            return false;
        }
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) {
            err << "error: cannot open '" << target.string() << "' for writing\n";
            return false;
        }
        out.write(reinterpret_cast<const char *>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        if (!out) {
            err << "error: write failed to '" << target.string() << "'\n";
            return false;
        }
        out.close();
        if (!out) {
            err << "error: write failed to '" << target.string() << "'\n";
            return false;
        }

        if constexpr (!zanna::platform::kHostWindows) {
            if (!makeExecutable)
                return true;
            std::error_code permEc;
            fs::permissions(target,
                            fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                                fs::perms::group_read | fs::perms::group_exec |
                                fs::perms::others_read | fs::perms::others_exec,
                            permEc);
            if (permEc) {
                err << "error: cannot set executable permissions on '" << target.string()
                    << "': " << permEc.message() << "\n";
                return false;
            }
        } else {
            (void)makeExecutable;
        }
        return true;
    };

    /// @brief Replace @p finalPath with @p tempPath without destroying the old file first.
    /// @details POSIX rename normally replaces the target atomically. When the
    ///          platform refuses to replace an existing target, this falls back to
    ///          moving the old file aside, installing the temp file, and restoring
    ///          the old file if installation fails.
    auto replaceWithTemp = [&](const fs::path &tempPath, const fs::path &finalPath) -> bool {
        std::error_code renameEc;
        fs::rename(tempPath, finalPath, renameEc);
        if (!renameEc)
            return true;

        std::error_code existsEc;
        if (!fs::exists(finalPath, existsEc) || existsEc) {
            err << "error: cannot replace '" << path << "': " << renameEc.message() << "\n";
            return false;
        }

        const fs::path backupPath = tempPath.string() + ".old";
        std::error_code cleanupEc;
        fs::remove(backupPath, cleanupEc);

        std::error_code backupEc;
        fs::rename(finalPath, backupPath, backupEc);
        if (backupEc) {
            err << "error: cannot move existing output '" << path
                << "' aside for replacement: " << backupEc.message() << "\n";
            return false;
        }

        renameEc.clear();
        fs::rename(tempPath, finalPath, renameEc);
        if (!renameEc) {
            fs::remove(backupPath, cleanupEc);
            return true;
        }

        std::error_code restoreEc;
        fs::rename(backupPath, finalPath, restoreEc);
        err << "error: cannot replace '" << path << "': " << renameEc.message();
        if (restoreEc)
            err << "; additionally failed to restore previous output: " << restoreEc.message();
        err << "\n";
        return false;
    };

    const fs::path finalPath(path);
    fs::path dir = finalPath.parent_path();
    if (dir.empty())
        dir = ".";

    static std::atomic<uint64_t> nonce{0};
    for (uint32_t attempt = 0; attempt < 32; ++attempt) {
        const uint64_t seed =
            static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
            nonce.fetch_add(1, std::memory_order_relaxed);
        const fs::path tempDir =
            dir / (finalPath.filename().string() + ".tmpdir." + std::to_string(seed + attempt));
        const fs::path tempPath = tempDir / finalPath.filename();

        std::error_code mkdirEc;
        if (!fs::create_directory(tempDir, mkdirEc)) {
            if (!mkdirEc)
                continue;
            err << "error: cannot create temporary output directory '" << tempDir.string()
                << "': " << mkdirEc.message() << "\n";
            return false;
        }

        auto cleanupTempDir = [&]() {
            std::error_code cleanupEc;
            fs::remove_all(tempDir, cleanupEc);
        };

        if constexpr (!zanna::platform::kHostWindows) {
            std::error_code permEc;
            fs::permissions(tempDir,
                            fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
                            fs::perm_options::replace,
                            permEc);
            if (permEc) {
                err << "error: cannot protect temporary output directory '" << tempDir.string()
                    << "': " << permEc.message() << "\n";
                cleanupTempDir();
                return false;
            }
        }

        std::error_code existsEc;
        if (fs::exists(tempPath, existsEc)) {
            cleanupTempDir();
            continue;
        }

        if (!writeDirect(tempPath)) {
            cleanupTempDir();
            return false;
        }

        if (replaceWithTemp(tempPath, finalPath)) {
            cleanupTempDir();
            return true;
        }

        cleanupTempDir();
        return false;
    }

    err << "error: cannot allocate temporary output path for '" << path << "'\n";
    return false;
}

} // namespace zanna::codegen::linker
