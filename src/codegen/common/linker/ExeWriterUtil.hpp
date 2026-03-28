//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace viper::codegen::linker {

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

/// Append a null-terminated string, padded or truncated to \p maxLen bytes.
inline void writeStr(std::vector<uint8_t> &buf, const char *s, size_t maxLen) {
    size_t len = std::strlen(s);
    size_t n = (len < maxLen) ? len : maxLen;
    buf.insert(buf.end(), s, s + n);
    if (n < maxLen)
        writePad(buf, maxLen - n);
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

/// Partition layout section indices into non-writable (text/rodata) and writable (data) groups.
/// Only includes sections with non-empty data.
inline void classifySections(const LinkLayout &layout,
                             std::vector<size_t> &textIndices,
                             std::vector<size_t> &dataIndices) {
    for (size_t i = 0; i < layout.sections.size(); ++i) {
        if (layout.sections[i].data.empty())
            continue;
        if (!layout.sections[i].alloc)
            continue; // Skip non-alloc sections (e.g., .debug_line).
        if (layout.sections[i].writable)
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
        size_t endOff = static_cast<size_t>(sec.virtualAddr + sec.data.size() - firstVA);
        if (endOff > span)
            span = endOff;
    }
    return span;
}

} // namespace viper::codegen::linker
