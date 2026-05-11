//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/ArchiveReader.cpp
// Purpose: Implementation of Unix ar archive parsing.
//          Handles GNU, BSD, and COFF archive format variants.
// Key invariants:
//   - GNU long names: "/offset" referencing "//" string table member
//   - BSD long names: "#1/N" with N name bytes following the header
//   - COFF: first "/" member is symbol table (big-endian count + offsets)
//   - Data is padded to 2-byte alignment boundary
// Links: codegen/common/linker/ArchiveReader.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/ArchiveReader.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>

namespace viper::codegen::linker {

static constexpr const char *kArMagic = "!<arch>\n";
static constexpr size_t kArMagicLen = 8;
static constexpr size_t kArHeaderLen = 60;

/// @brief Add @p a + @p b into @p out, returning false on size_t overflow.
/// @details Used throughout this reader to harden against malformed archives
///          that claim sizes near SIZE_MAX in their member headers.
static bool checkedAdd(size_t a, size_t b, size_t &out) {
    if (a > std::numeric_limits<size_t>::max() - b)
        return false;
    out = a + b;
    return true;
}

/// @brief Verify that the byte range [@p off, @p off+@p len) fits within @p size.
/// @details Avoids the @c off+len overflow trap by computing the bound as
///          @p size − @p off, which never overflows once @p off ≤ @p size holds.
static bool checkedRange(size_t off, size_t len, size_t size) {
    return off <= size && len <= size - off;
}

/// Read a big-endian 32-bit integer from raw bytes.
static uint32_t readBE32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

/// Read a little-endian 16-bit integer from raw bytes.
static uint16_t readLE16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

/// Read a little-endian 32-bit integer from raw bytes.
static uint32_t readLE32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/// Maximum archive member size: 2 GB.
static constexpr size_t kMaxMemberSize = 2ULL * 1024 * 1024 * 1024;

/// Parse the size field from an archive member header (ASCII decimal, 10 chars at offset 48).
/// Returns SIZE_MAX on overflow or if the value exceeds kMaxMemberSize.
static size_t parseSize(const uint8_t *header) {
    // Size field is at offset 48, 10 bytes, ASCII decimal, space-padded.
    size_t val = 0;
    bool sawDigit = false;
    bool sawPadding = false;
    for (int i = 48; i < 58; ++i) {
        if (header[i] >= '0' && header[i] <= '9') {
            if (sawPadding)
                return SIZE_MAX;
            sawDigit = true;
            const size_t digit = static_cast<size_t>(header[i] - '0');
            if (val > (std::numeric_limits<size_t>::max() - digit) / 10)
                return SIZE_MAX;
            val = val * 10 + digit;
        } else if (header[i] == ' ') {
            if (sawDigit)
                sawPadding = true;
        } else {
            return SIZE_MAX;
        }
    }
    if (!sawDigit)
        return SIZE_MAX;
    return val > kMaxMemberSize ? SIZE_MAX : val;
}

/// Parse the name field from an archive member header (16 chars at offset 0).
/// Returns the trimmed name and whether this is a special member.
[[maybe_unused]] static std::string parseName(const uint8_t *header, const std::string &longNames) {
    char raw[17] = {};
    std::memcpy(raw, header, 16);
    std::string name(raw, 16);

    // BSD long name: "#1/N" — N bytes of name follow the header.
    if (name.size() >= 3 && name[0] == '#' && name[1] == '1' && name[2] == '/') {
        size_t nameLen = 0;
        for (size_t i = 3; i < name.size() && name[i] >= '0' && name[i] <= '9'; ++i)
            nameLen = nameLen * 10 + (name[i] - '0');
        // Name bytes are at the start of member data; we'll extract in the caller.
        // Return a marker so the caller knows.
        return "#1/" + std::to_string(nameLen);
    }

    // GNU long name: "/offset" referencing the "//" string table.
    if (name[0] == '/' && name[1] >= '0' && name[1] <= '9') {
        size_t offset = 0;
        for (size_t i = 1; i < name.size() && name[i] >= '0' && name[i] <= '9'; ++i)
            offset = offset * 10 + (name[i] - '0');
        if (offset < longNames.size()) {
            size_t end = longNames.find('/', offset);
            if (end == std::string::npos)
                end = longNames.find('\n', offset);
            if (end == std::string::npos)
                end = longNames.size();
            return longNames.substr(offset, end - offset);
        }
    }

    // Trim trailing spaces and '/' (GNU terminator).
    while (!name.empty() && (name.back() == ' ' || name.back() == '/'))
        name.pop_back();
    return name;
}

/// Parse a GNU-style symbol table ("/" member).
/// Format: big-endian 32-bit count, then count big-endian 32-bit offsets,
/// then count NUL-terminated symbol names.
static void parseGnuSymbolTable(const uint8_t *data,
                                size_t size,
                                std::vector<std::pair<std::string, size_t>> &symbols) {
    if (size < 4)
        return;
    const uint32_t count = readBE32(data);
    size_t offsetsBytes = static_cast<size_t>(count) * 4;
    if (count != 0 && offsetsBytes / 4 != count)
        return;
    size_t namesOff = 0;
    if (!checkedAdd(4, offsetsBytes, namesOff) || namesOff > size)
        return;

    // Offsets array.
    std::vector<uint32_t> offsets(count);
    for (uint32_t i = 0; i < count; ++i)
        offsets[i] = readBE32(data + 4 + i * 4);

    // Symbol names follow offsets, NUL-terminated.
    const char *namePtr = reinterpret_cast<const char *>(data + namesOff);
    const char *nameEnd = reinterpret_cast<const char *>(data + size);
    for (uint32_t i = 0; i < count && namePtr < nameEnd; ++i) {
        const char *nul = std::find(namePtr, nameEnd, '\0');
        if (nul == nameEnd)
            break;
        std::string symName(namePtr, nul);
        symbols.emplace_back(std::move(symName), offsets[i]);
        namePtr = nul + 1;
    }
}

/// Parse a BSD-style symbol table ("__.SYMDEF" or "__.SYMDEF SORTED").
/// Format: 4-byte ranlib count (byte size of ranlib array), then ranlibs (8B each:
/// string offset + member offset), then 4-byte string size, then string pool.
static void parseBsdSymbolTable(const uint8_t *data,
                                size_t size,
                                std::vector<std::pair<std::string, size_t>> &symbols) {
    if (size < 4)
        return;
    // Little-endian 4-byte: byte size of ranlib array.
    auto readLE32 = [](const uint8_t *p) -> uint32_t {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    };

    const uint32_t ranlibSize = readLE32(data);
    if ((ranlibSize % 8) != 0)
        return;
    const uint32_t ranlibCount = ranlibSize / 8;
    size_t strSizeOff = 0;
    if (!checkedAdd(4, ranlibSize, strSizeOff) || !checkedRange(strSizeOff, 4, size))
        return;

    const uint8_t *ranlibData = data + 4;
    const uint32_t strSize = readLE32(data + strSizeOff);
    size_t strPoolOff = 0;
    if (!checkedAdd(strSizeOff, 4, strPoolOff) || !checkedRange(strPoolOff, strSize, size))
        return;
    const char *strPool = reinterpret_cast<const char *>(data + strPoolOff);
    const char *strEnd = strPool + strSize;

    for (uint32_t i = 0; i < ranlibCount; ++i) {
        const uint32_t strOff = readLE32(ranlibData + i * 8);
        const uint32_t memberOff = readLE32(ranlibData + i * 8 + 4);
        if (strOff < strSize) {
            const char *symStart = strPool + strOff;
            const char *nul = std::find(symStart, strEnd, '\0');
            if (nul == strEnd)
                continue;
            std::string symName(symStart, nul);
            symbols.emplace_back(std::move(symName), memberOff);
        }
    }
}

/// Parse the Microsoft COFF second linker member (the preferred symbol index).
/// Format:
///   u32le memberCount
///   u32le offsets[memberCount]
///   u32le symbolCount
///   u16le indices[symbolCount]   (1-based into offsets[])
///   char names[][NUL]
static void parseCoffSecondLinkerMember(const uint8_t *data,
                                        size_t size,
                                        std::vector<std::pair<std::string, size_t>> &symbols) {
    if (size < 8)
        return;

    const uint32_t memberCount = readLE32(data);
    const size_t offsetsBytes = static_cast<size_t>(memberCount) * 4;
    if (memberCount != 0 && offsetsBytes / 4 != memberCount)
        return;
    size_t symbolCountOff = 0;
    if (!checkedAdd(4, offsetsBytes, symbolCountOff) || !checkedRange(symbolCountOff, 4, size))
        return;

    const uint8_t *offsets = data + 4;
    const uint8_t *symbolCountPtr = data + symbolCountOff;
    const uint32_t symbolCount = readLE32(symbolCountPtr);
    const size_t indexBytes = static_cast<size_t>(symbolCount) * 2;
    if (symbolCount != 0 && indexBytes / 2 != symbolCount)
        return;
    size_t indexEndOff = 0;
    if (!checkedAdd(symbolCountOff, 4, indexEndOff))
        return;
    size_t namesOff = 0;
    if (!checkedAdd(indexEndOff, indexBytes, namesOff) || namesOff > size)
        return;

    const uint8_t *indices = symbolCountPtr + 4;
    const char *namePtr = reinterpret_cast<const char *>(data + namesOff);
    const char *nameEnd = reinterpret_cast<const char *>(data + size);

    for (uint32_t i = 0; i < symbolCount && namePtr < nameEnd; ++i) {
        const uint16_t memberIndex = readLE16(indices + static_cast<size_t>(i) * 2);
        const char *nul = std::find(namePtr, nameEnd, '\0');
        if (nul == nameEnd)
            break;

        if (memberIndex > 0 && memberIndex <= memberCount) {
            const uint32_t memberOffset =
                readLE32(offsets + static_cast<size_t>(memberIndex - 1) * 4);
            symbols.emplace_back(std::string(namePtr, nul), memberOffset);
        }

        namePtr = nul + 1;
    }
}

bool readArchive(const std::string &path, Archive &ar, std::ostream &err) {
    // Read the entire file.
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        err << "error: cannot open archive '" << path << "'\n";
        return false;
    }
    const std::streampos endPos = f.tellg();
    if (endPos == std::streampos(-1)) {
        err << "error: failed to determine archive size for '" << path << "'\n";
        return false;
    }
    const auto endOff = static_cast<std::streamoff>(endPos);
    if (endOff < 0 ||
        static_cast<uintmax_t>(endOff) >
            static_cast<uintmax_t>(std::numeric_limits<size_t>::max()) ||
        static_cast<uintmax_t>(endOff) >
            static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        err << "error: archive '" << path << "' is too large to read\n";
        return false;
    }
    const auto fileSize = static_cast<size_t>(endOff);
    f.seekg(0);
    ar.data.resize(fileSize);
    f.read(reinterpret_cast<char *>(ar.data.data()), static_cast<std::streamsize>(fileSize));
    if (!f) {
        err << "error: failed to read archive '" << path << "'\n";
        return false;
    }

    ar.path = path;

    // Verify magic.
    if (fileSize < kArMagicLen || std::memcmp(ar.data.data(), kArMagic, kArMagicLen) != 0) {
        err << "error: not an archive file: '" << path << "'\n";
        return false;
    }

    // First pass: find special members (symbol table, long name string table).
    std::string longNames;                                  // GNU "//" long name string table.
    std::vector<std::pair<std::string, size_t>> rawSymbols; // (symbol name, file offset of member).

    // Track member headers and their file offsets for symbol index mapping.
    struct RawMember {
        std::string name;
        size_t headerOffset;
        size_t dataOffset;
        size_t dataSize;
        size_t bsdNameLen; // BSD "#1/N" name length (0 if not BSD long name).
        bool isSpecial;    // true for "/", "//", "__.SYMDEF"
    };

    std::vector<RawMember> rawMembers;

    size_t pos = kArMagicLen;
    size_t coffLinkerMembersSeen = 0;
    while (pos <= fileSize && kArHeaderLen <= fileSize - pos) {
        const uint8_t *header = ar.data.data() + pos;

        // Verify header end magic "'\n".
        if (header[58] != '`' || header[59] != '\n')
            break;

        size_t memberSize = parseSize(header);
        if (memberSize == SIZE_MAX) {
            err << "error: archive member at offset " << pos << " has invalid size in '" << path
                << "'\n";
            return false;
        }
        size_t dataStart = 0;
        if (!checkedAdd(pos, kArHeaderLen, dataStart) ||
            !checkedRange(dataStart, memberSize, fileSize)) {
            err << "error: archive member at offset " << pos << " extends beyond file in '" << path
                << "'\n";
            return false;
        }

        // Parse raw name field.
        char rawName[17] = {};
        std::memcpy(rawName, header, 16);
        std::string nameField(rawName, 16);
        while (!nameField.empty() && nameField.back() == ' ')
            nameField.pop_back();

        bool isSpecial = false;
        std::string memberName;

        // Resolve BSD "#1/N" long names BEFORE checking for special members,
        // because macOS archives store "__.SYMDEF SORTED" as a long name.
        std::string resolvedName;
        size_t bsdNameLen = 0;
        if (nameField.size() >= 3 && nameField[0] == '#' && nameField[1] == '1' &&
            nameField[2] == '/') {
            bool sawNameLenDigit = false;
            for (size_t i = 3; i < nameField.size() && nameField[i] >= '0' && nameField[i] <= '9';
                 ++i) {
                sawNameLenDigit = true;
                const size_t digit = static_cast<size_t>(nameField[i] - '0');
                if (bsdNameLen > (std::numeric_limits<size_t>::max() - digit) / 10) {
                    err << "error: archive member at offset " << pos
                        << " has invalid BSD long-name length in '" << path << "'\n";
                    return false;
                }
                bsdNameLen = bsdNameLen * 10 + digit;
            }
            if (!sawNameLenDigit || bsdNameLen > memberSize ||
                !checkedRange(dataStart, bsdNameLen, fileSize)) {
                err << "error: archive member at offset " << pos
                    << " has invalid BSD long-name length in '" << path << "'\n";
                return false;
            }
            const char *nd = reinterpret_cast<const char *>(ar.data.data() + dataStart);
            size_t effLen = bsdNameLen;
            while (effLen > 0 && nd[effLen - 1] == '\0')
                --effLen;
            resolvedName.assign(nd, effLen);
        } else {
            resolvedName = nameField;
            if (resolvedName == "/" || resolvedName == "//") {
                // Keep the COFF/GNU special member names intact.
            } else if (resolvedName.size() > 1 && resolvedName[0] == '/' &&
                       resolvedName[1] >= '0' && resolvedName[1] <= '9') {
                size_t offset = 0;
                for (size_t i = 1;
                     i < resolvedName.size() && resolvedName[i] >= '0' && resolvedName[i] <= '9';
                     ++i) {
                    const size_t digit = static_cast<size_t>(resolvedName[i] - '0');
                    if (offset > (std::numeric_limits<size_t>::max() - digit) / 10) {
                        offset = std::numeric_limits<size_t>::max();
                        break;
                    }
                    offset = offset * 10 + digit;
                }
                if (offset < longNames.size()) {
                    size_t end = offset;
                    while (end < longNames.size() && longNames[end] != '\0')
                        ++end;
                    resolvedName = longNames.substr(offset, end - offset);
                }
            } else {
                // Trim trailing '/' (GNU terminator) for normal short names.
                while (!resolvedName.empty() && resolvedName.back() == '/')
                    resolvedName.pop_back();
            }
        }

        // Check for special members.
        if (resolvedName == "/" || resolvedName == "__.SYMDEF" ||
            resolvedName == "__.SYMDEF SORTED") {
            isSpecial = true;
            // For BSD long names, the symbol data starts AFTER the name bytes.
            size_t symDataOff = 0;
            size_t symDataSize = memberSize - bsdNameLen;
            if (checkedAdd(dataStart, bsdNameLen, symDataOff) &&
                checkedRange(symDataOff, symDataSize, fileSize)) {
                if (resolvedName == "/") {
                    ++coffLinkerMembersSeen;
                    if (coffLinkerMembersSeen == 2)
                        parseCoffSecondLinkerMember(
                            ar.data.data() + symDataOff, symDataSize, rawSymbols);
                    else
                        parseGnuSymbolTable(ar.data.data() + symDataOff, symDataSize, rawSymbols);
                } else {
                    parseBsdSymbolTable(ar.data.data() + symDataOff, symDataSize, rawSymbols);
                }
            }
        } else if (resolvedName == "//" || nameField == "//") {
            isSpecial = true;
            // GNU long name string table.
            if (checkedRange(dataStart, memberSize, fileSize))
                longNames.assign(reinterpret_cast<const char *>(ar.data.data() + dataStart),
                                 memberSize);
        } else {
            // Regular member.
            memberName = resolvedName;
        }

        rawMembers.push_back({memberName, pos, dataStart, memberSize, bsdNameLen, isSpecial});

        // Advance past data, aligned to 2 bytes.
        if (!checkedAdd(dataStart, memberSize, pos)) {
            err << "error: archive member at offset " << dataStart
                << " exceeds addressable size in '" << path << "'\n";
            return false;
        }
        if (pos & 1) {
            if (pos == std::numeric_limits<size_t>::max()) {
                err << "error: archive member alignment exceeds addressable size in '" << path
                    << "'\n";
                return false;
            }
            ++pos;
        }
    }

    // Second pass: build member list (non-special members only).
    // BSD long names are already resolved; just adjust data offsets.
    for (auto &rm : rawMembers) {
        if (rm.isSpecial)
            continue;

        size_t actualDataOffset = rm.dataOffset + rm.bsdNameLen;
        size_t actualDataSize = rm.dataSize - rm.bsdNameLen;

        ar.members.push_back({rm.name, actualDataOffset, actualDataSize});
    }

    // Build symbol → member index map.
    // rawSymbols has (symbol name, file offset of member header).
    // We need to map file offsets → member indices.
    std::unordered_map<size_t, size_t> headerOffsetToIdx;
    size_t memberIdx = 0;
    for (size_t i = 0; i < rawMembers.size(); ++i) {
        if (!rawMembers[i].isSpecial) {
            headerOffsetToIdx[rawMembers[i].headerOffset] = memberIdx;
            ++memberIdx;
        }
    }

    for (const auto &[symName, fileOffset] : rawSymbols) {
        auto it = headerOffsetToIdx.find(fileOffset);
        if (it != headerOffsetToIdx.end()) {
            ar.symbolIndex.emplace(symName, it->second);
            ar.symbolCandidates[symName].push_back(it->second);
        }
    }

    return true;
}

std::vector<uint8_t> extractMember(const Archive &ar, const ArchiveMember &member) {
    if (!checkedRange(member.dataOffset, member.dataSize, ar.data.size()))
        return {};
    return std::vector<uint8_t>(
        ar.data.begin() + static_cast<std::ptrdiff_t>(member.dataOffset),
        ar.data.begin() + static_cast<std::ptrdiff_t>(member.dataOffset + member.dataSize));
}

ArchiveMemberView memberDataView(const Archive &ar, const ArchiveMember &member) {
    if (!checkedRange(member.dataOffset, member.dataSize, ar.data.size()))
        return {};
    return ArchiveMemberView{ar.data.data() + member.dataOffset, member.dataSize};
}

} // namespace viper::codegen::linker
