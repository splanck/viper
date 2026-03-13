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
#include <cstring>
#include <fstream>

namespace viper::codegen::linker
{

static constexpr const char *kArMagic = "!<arch>\n";
static constexpr size_t kArMagicLen = 8;
static constexpr size_t kArHeaderLen = 60;

/// Read a big-endian 32-bit integer from raw bytes.
static uint32_t readBE32(const uint8_t *p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

/// Maximum archive member size: 2 GB.
static constexpr size_t kMaxMemberSize = 2ULL * 1024 * 1024 * 1024;

/// Parse the size field from an archive member header (ASCII decimal, 10 chars at offset 48).
/// Returns SIZE_MAX on overflow or if the value exceeds kMaxMemberSize.
static size_t parseSize(const uint8_t *header)
{
    // Size field is at offset 48, 10 bytes, ASCII decimal, space-padded.
    size_t val = 0;
    for (int i = 48; i < 58; ++i)
    {
        if (header[i] >= '0' && header[i] <= '9')
        {
            size_t prev = val;
            val = val * 10 + (header[i] - '0');
            if (val < prev) // Overflow.
                return SIZE_MAX;
        }
        else
            break;
    }
    return val > kMaxMemberSize ? SIZE_MAX : val;
}

/// Parse the name field from an archive member header (16 chars at offset 0).
/// Returns the trimmed name and whether this is a special member.
static std::string parseName(const uint8_t *header, const std::string &longNames)
{
    char raw[17] = {};
    std::memcpy(raw, header, 16);
    std::string name(raw, 16);

    // BSD long name: "#1/N" — N bytes of name follow the header.
    if (name.size() >= 3 && name[0] == '#' && name[1] == '1' && name[2] == '/')
    {
        size_t nameLen = 0;
        for (size_t i = 3; i < name.size() && name[i] >= '0' && name[i] <= '9'; ++i)
            nameLen = nameLen * 10 + (name[i] - '0');
        // Name bytes are at the start of member data; we'll extract in the caller.
        // Return a marker so the caller knows.
        return "#1/" + std::to_string(nameLen);
    }

    // GNU long name: "/offset" referencing the "//" string table.
    if (name[0] == '/' && name[1] >= '0' && name[1] <= '9')
    {
        size_t offset = 0;
        for (size_t i = 1; i < name.size() && name[i] >= '0' && name[i] <= '9'; ++i)
            offset = offset * 10 + (name[i] - '0');
        if (offset < longNames.size())
        {
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
static void parseGnuSymbolTable(const uint8_t *data, size_t size,
                                 std::vector<std::pair<std::string, size_t>> &symbols)
{
    if (size < 4)
        return;
    const uint32_t count = readBE32(data);
    if (size < 4 + count * 4u)
        return;

    // Offsets array.
    std::vector<uint32_t> offsets(count);
    for (uint32_t i = 0; i < count; ++i)
        offsets[i] = readBE32(data + 4 + i * 4);

    // Symbol names follow offsets, NUL-terminated.
    const char *namePtr = reinterpret_cast<const char *>(data + 4 + count * 4);
    const char *nameEnd = reinterpret_cast<const char *>(data + size);
    for (uint32_t i = 0; i < count && namePtr < nameEnd; ++i)
    {
        std::string symName(namePtr);
        symbols.emplace_back(std::move(symName), offsets[i]);
        namePtr += symbols.back().first.size() + 1;
    }
}

/// Parse a BSD-style symbol table ("__.SYMDEF" or "__.SYMDEF SORTED").
/// Format: 4-byte ranlib count (byte size of ranlib array), then ranlibs (8B each:
/// string offset + member offset), then 4-byte string size, then string pool.
static void parseBsdSymbolTable(const uint8_t *data, size_t size,
                                 std::vector<std::pair<std::string, size_t>> &symbols)
{
    if (size < 4)
        return;
    // Little-endian 4-byte: byte size of ranlib array.
    auto readLE32 = [](const uint8_t *p) -> uint32_t
    {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    };

    const uint32_t ranlibSize = readLE32(data);
    const uint32_t ranlibCount = ranlibSize / 8;
    if (size < 4 + ranlibSize + 4)
        return;

    const uint8_t *ranlibData = data + 4;
    const uint32_t strSize = readLE32(data + 4 + ranlibSize);
    const char *strPool = reinterpret_cast<const char *>(data + 4 + ranlibSize + 4);

    for (uint32_t i = 0; i < ranlibCount; ++i)
    {
        const uint32_t strOff = readLE32(ranlibData + i * 8);
        const uint32_t memberOff = readLE32(ranlibData + i * 8 + 4);
        if (strOff < strSize)
        {
            std::string symName(strPool + strOff);
            symbols.emplace_back(std::move(symName), memberOff);
        }
    }
}

bool readArchive(const std::string &path, Archive &ar, std::ostream &err)
{
    // Read the entire file.
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
    {
        err << "error: cannot open archive '" << path << "'\n";
        return false;
    }
    const auto fileSize = static_cast<size_t>(f.tellg());
    f.seekg(0);
    ar.data.resize(fileSize);
    f.read(reinterpret_cast<char *>(ar.data.data()), static_cast<std::streamsize>(fileSize));
    if (!f)
    {
        err << "error: failed to read archive '" << path << "'\n";
        return false;
    }

    ar.path = path;

    // Verify magic.
    if (fileSize < kArMagicLen || std::memcmp(ar.data.data(), kArMagic, kArMagicLen) != 0)
    {
        err << "error: not an archive file: '" << path << "'\n";
        return false;
    }

    // First pass: find special members (symbol table, long name string table).
    std::string longNames; // GNU "//" long name string table.
    std::vector<std::pair<std::string, size_t>> rawSymbols; // (symbol name, file offset of member).

    // Track member headers and their file offsets for symbol index mapping.
    struct RawMember
    {
        std::string name;
        size_t headerOffset;
        size_t dataOffset;
        size_t dataSize;
        size_t bsdNameLen; // BSD "#1/N" name length (0 if not BSD long name).
        bool isSpecial;    // true for "/", "//", "__.SYMDEF"
    };
    std::vector<RawMember> rawMembers;

    size_t pos = kArMagicLen;
    while (pos + kArHeaderLen <= fileSize)
    {
        const uint8_t *header = ar.data.data() + pos;

        // Verify header end magic "'\n".
        if (header[58] != '`' || header[59] != '\n')
            break;

        size_t memberSize = parseSize(header);
        if (memberSize == SIZE_MAX)
        {
            err << "error: archive member at offset " << pos << " has invalid size in '" << path
                << "'\n";
            return false;
        }
        size_t dataStart = pos + kArHeaderLen;
        if (dataStart + memberSize > fileSize)
        {
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
            nameField[2] == '/')
        {
            for (size_t i = 3; i < nameField.size() && nameField[i] >= '0' &&
                                   nameField[i] <= '9';
                 ++i)
                bsdNameLen = bsdNameLen * 10 + (nameField[i] - '0');
            if (bsdNameLen <= memberSize && dataStart + bsdNameLen <= fileSize)
            {
                const char *nd = reinterpret_cast<const char *>(ar.data.data() + dataStart);
                size_t effLen = bsdNameLen;
                while (effLen > 0 && nd[effLen - 1] == '\0')
                    --effLen;
                resolvedName.assign(nd, effLen);
            }
        }
        else
        {
            resolvedName = nameField;
            // Trim trailing '/' (GNU terminator).
            while (!resolvedName.empty() && resolvedName.back() == '/')
                resolvedName.pop_back();
        }

        // Check for special members.
        if (resolvedName == "/" || resolvedName == "__.SYMDEF" ||
            resolvedName == "__.SYMDEF SORTED")
        {
            isSpecial = true;
            // For BSD long names, the symbol data starts AFTER the name bytes.
            size_t symDataOff = dataStart + bsdNameLen;
            size_t symDataSize = memberSize - bsdNameLen;
            if (symDataOff + symDataSize <= fileSize)
            {
                if (resolvedName == "/")
                    parseGnuSymbolTable(ar.data.data() + symDataOff, symDataSize, rawSymbols);
                else
                    parseBsdSymbolTable(ar.data.data() + symDataOff, symDataSize, rawSymbols);
            }
        }
        else if (resolvedName == "//" || nameField == "//")
        {
            isSpecial = true;
            // GNU long name string table.
            if (dataStart + memberSize <= fileSize)
                longNames.assign(reinterpret_cast<const char *>(ar.data.data() + dataStart),
                                 memberSize);
        }
        else
        {
            // Regular member.
            memberName = resolvedName;
        }

        rawMembers.push_back({memberName, pos, dataStart, memberSize, bsdNameLen, isSpecial});

        // Advance past data, aligned to 2 bytes.
        pos = dataStart + memberSize;
        if (pos & 1)
            ++pos;
    }

    // Second pass: build member list (non-special members only).
    // BSD long names are already resolved; just adjust data offsets.
    for (auto &rm : rawMembers)
    {
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
    for (size_t i = 0; i < rawMembers.size(); ++i)
    {
        if (!rawMembers[i].isSpecial)
        {
            headerOffsetToIdx[rawMembers[i].headerOffset] = memberIdx;
            ++memberIdx;
        }
    }

    for (const auto &[symName, fileOffset] : rawSymbols)
    {
        auto it = headerOffsetToIdx.find(fileOffset);
        if (it != headerOffsetToIdx.end())
            ar.symbolIndex[symName] = it->second;
    }

    return true;
}

std::vector<uint8_t> extractMember(const Archive &ar, const ArchiveMember &member)
{
    if (member.dataOffset + member.dataSize > ar.data.size())
        return {};
    return std::vector<uint8_t>(ar.data.begin() + static_cast<std::ptrdiff_t>(member.dataOffset),
                                 ar.data.begin() +
                                     static_cast<std::ptrdiff_t>(member.dataOffset + member.dataSize));
}

} // namespace viper::codegen::linker
