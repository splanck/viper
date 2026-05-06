//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PkgVerify.cpp
// Purpose: Post-build verification of generated packages.
//
// Key invariants:
//   - Read-only: never modifies input data.
//   - Returns false on first structural error found.
//
// Ownership/Lifetime:
//   - Pure functions, no state.
//
// Links: PkgVerify.hpp
//
//===----------------------------------------------------------------------===//

#include "PkgVerify.hpp"
#include "PkgGzip.hpp"
#include "PkgUtils.hpp"
#include "ZipReader.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <set>
#include <sstream>

namespace viper::pkg {

namespace {

// Read a little-endian uint16_t from an unaligned byte pointer.
uint16_t rdLE16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

// Read a little-endian uint32_t from an unaligned byte pointer.
uint32_t rdLE32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Return true if the byte range [offset, offset+length) is entirely within [0, size).
// Used to guard all bounds-checked reads from archive and PE structures.
bool hasRange(size_t offset, size_t length, size_t size) {
    return offset <= size && length <= size - offset;
}

// Return true if all 512 bytes of a USTAR tar block are zero.
// Two consecutive zero blocks mark the end-of-archive in the POSIX tar format.
bool isAllZeroBlock(const uint8_t *p) {
    for (size_t i = 0; i < 512; ++i) {
        if (p[i] != 0)
            return false;
    }
    return true;
}

// Parse a fixed-width space/NUL-terminated decimal field (used in ar member headers).
// Returns false if the field contains non-digit characters before the terminator,
// or if the value overflows size_t.
bool parseDecimalField(const uint8_t *field, size_t width, size_t &out) {
    out = 0;
    bool sawDigit = false;
    for (size_t i = 0; i < width; ++i) {
        const unsigned char c = field[i];
        if (c == ' ' || c == '\0') {
            for (++i; i < width; ++i) {
                if (field[i] != ' ' && field[i] != '\0')
                    return false;
            }
            return sawDigit;
        }
        if (!std::isdigit(c))
            return false;
        sawDigit = true;
        if (out > (static_cast<size_t>(-1) - (c - '0')) / 10)
            return false;
        out = out * 10 + static_cast<size_t>(c - '0');
    }
    return sawDigit;
}

// Parse a fixed-width space/NUL-terminated octal field (used in USTAR tar headers
// for file size, checksum, mode, etc.). Returns false if the field contains
// non-octal characters before the terminator, or if the value overflows uint64_t.
bool parseOctalField(const uint8_t *field, size_t width, uint64_t &out) {
    out = 0;
    bool sawDigit = false;
    for (size_t i = 0; i < width; ++i) {
        const unsigned char c = field[i];
        if (c == ' ' || c == '\0') {
            for (++i; i < width; ++i) {
                if (field[i] != ' ' && field[i] != '\0')
                    return false;
            }
            return sawDigit;
        }
        if (c < '0' || c > '7')
            return false;
        sawDigit = true;
        if (out > (static_cast<uint64_t>(-1) - (c - '0')) / 8)
            return false;
        out = (out << 3) + static_cast<uint64_t>(c - '0');
    }
    return sawDigit;
}

// Extract a NUL-terminated string from a fixed-width tar header field.
// Returns everything up to the first NUL byte, or all width bytes if none.
std::string tarFieldString(const uint8_t *field, size_t width) {
    size_t len = 0;
    while (len < width && field[len] != '\0')
        ++len;
    return std::string(reinterpret_cast<const char *>(field), len);
}

// Compute the POSIX USTAR header checksum: sum all 512 bytes treating the
// checksum field at bytes 148-155 as if they were spaces (0x20).
// The stored octal value must equal this sum for the header to be valid.
uint32_t tarChecksum(const uint8_t *hdr) {
    uint32_t sum = 0;
    for (size_t i = 0; i < 512; ++i)
        sum += (i >= 148 && i < 156) ? static_cast<uint8_t>(' ') : hdr[i];
    return sum;
}

// Verify a raw (already decompressed) USTAR tar stream.
// Checks: 512-byte-aligned size, ustar magic, checksums, safe relative paths,
// non-duplicate entries, valid symlink targets (no escapes), and two end-of-archive
// zero blocks. Optionally collects normalized entry names into *outNames.
bool verifyTarBytes(const std::vector<uint8_t> &data,
                    std::ostream &err,
                    std::set<std::string> *outNames = nullptr) {
    if (data.size() < 1024 || data.size() % 512 != 0) {
        err << "TAR: archive size is not a valid 512-byte-block tar stream\n";
        return false;
    }

    size_t pos = 0;
    bool sawEnd = false;
    std::set<std::string> seenNames;
    while (pos + 512 <= data.size()) {
        const uint8_t *hdr = data.data() + pos;
        if (isAllZeroBlock(hdr)) {
            if (pos + 1024 > data.size() || !isAllZeroBlock(data.data() + pos + 512)) {
                err << "TAR: missing second zero end-of-archive block\n";
                return false;
            }
            sawEnd = true;
            for (size_t tail = pos + 1024; tail < data.size(); ++tail) {
                if (data[tail] != 0) {
                    err << "TAR: non-zero bytes after end-of-archive marker\n";
                    return false;
                }
            }
            break;
        }

        if (std::memcmp(hdr + 257, "ustar", 5) != 0) {
            err << "TAR: missing ustar magic at offset " << pos << "\n";
            return false;
        }

        uint64_t storedChecksum = 0;
        if (!parseOctalField(hdr + 148, 8, storedChecksum)) {
            err << "TAR: invalid checksum field at offset " << pos << "\n";
            return false;
        }
        if (storedChecksum != tarChecksum(hdr)) {
            err << "TAR: checksum mismatch at offset " << pos << "\n";
            return false;
        }

        uint64_t fileSize = 0;
        if (!parseOctalField(hdr + 124, 12, fileSize)) {
            err << "TAR: invalid size field at offset " << pos << "\n";
            return false;
        }

        std::string name = tarFieldString(hdr, 100);
        const std::string prefix = tarFieldString(hdr + 345, 155);
        if (!prefix.empty())
            name = prefix + "/" + name;
        if (name.rfind("./", 0) == 0)
            name = name.substr(2);
        if (!name.empty()) {
            try {
                const bool isDir = hdr[156] == '5';
                const std::string clean =
                    sanitizePackageRelativePath(name, isDir ? "tar directory path" : "tar path");
                if (clean.empty() && !isDir) {
                    err << "TAR: empty file path at offset " << pos << "\n";
                    return false;
                }
                if (!clean.empty() && !seenNames.insert(clean).second) {
                    err << "TAR: duplicate entry '" << clean << "' at offset " << pos << "\n";
                    return false;
                }
                if (outNames)
                    outNames->insert(clean);
            } catch (const std::exception &ex) {
                err << "TAR: unsafe path '" << name << "': " << ex.what() << "\n";
                return false;
            }
        }

        const char type = hdr[156] == '\0' ? '0' : static_cast<char>(hdr[156]);
        if (name.empty() && type != '5') {
            err << "TAR: empty entry path at offset " << pos << "\n";
            return false;
        }
        if (type != '0' && type != '5' && type != '2') {
            err << "TAR: unsupported entry type '" << type << "' at offset " << pos << "\n";
            return false;
        }
        if (type == '2') {
            std::string target = tarFieldString(hdr + 157, 100);
            if (target.empty()) {
                err << "TAR: empty symlink target at offset " << pos << "\n";
                return false;
            }
            try {
                validateSingleLineField(target, "tar symlink target");
                for (char &c : target) {
                    if (c == '\\')
                        c = '/';
                }
                if (target.front() == '/' ||
                    (target.size() >= 2 &&
                     std::isalpha(static_cast<unsigned char>(target[0])) && target[1] == ':')) {
                    err << "TAR: absolute symlink target '" << target << "' at offset " << pos
                        << "\n";
                    return false;
                }
                const std::filesystem::path resolved =
                    (std::filesystem::path(name).parent_path() / target).lexically_normal();
                const std::string resolvedText = resolved.generic_string();
                if (resolvedText.empty() || resolvedText == "." ||
                    resolvedText.rfind("../", 0) == 0 || resolvedText == "..") {
                    err << "TAR: symlink target escapes archive root at offset " << pos << "\n";
                    return false;
                }
            } catch (const std::exception &ex) {
                err << "TAR: unsafe symlink target '" << target << "': " << ex.what() << "\n";
                return false;
            }
        }
        if (type != '0' && fileSize != 0) {
            err << "TAR: non-file entry carries data at offset " << pos << "\n";
            return false;
        }

        const size_t paddedSize =
            static_cast<size_t>((fileSize + 511u) & ~static_cast<uint64_t>(511u));
        if (fileSize > static_cast<uint64_t>(data.size() - pos - 512) ||
            paddedSize > data.size() - pos - 512) {
            err << "TAR: entry data extends past end of archive\n";
            return false;
        }
        pos += 512 + paddedSize;
    }

    if (!sawEnd) {
        err << "TAR: missing end-of-archive marker\n";
        return false;
    }
    return true;
}

// Compute the byte offset where overlay data begins in a PE file.
// Parses the DOS/COFF/section headers to find the end of the last section's
// raw data, then sets overlayOff to that position. Returns false (with a
// message to err) if the PE structure is malformed.
bool parsePeOverlayOffset(const std::vector<uint8_t> &data, size_t &overlayOff, std::ostream &err) {
    if (data.size() < 64) {
        err << "PE: file too small (" << data.size() << " bytes)\n";
        return false;
    }
    if (data[0] != 'M' || data[1] != 'Z') {
        err << "PE: missing DOS 'MZ' magic\n";
        return false;
    }

    const size_t peOff = static_cast<size_t>(rdLE32(data.data() + 60));
    if (!hasRange(peOff, 4, data.size())) {
        err << "PE: e_lfanew (" << peOff << ") points past end of file\n";
        return false;
    }
    if (data[peOff] != 'P' || data[peOff + 1] != 'E' || data[peOff + 2] != 0 ||
        data[peOff + 3] != 0) {
        err << "PE: invalid PE signature at offset " << peOff << "\n";
        return false;
    }

    const size_t coffOff = peOff + 4;
    if (!hasRange(coffOff, 20, data.size())) {
        err << "PE: COFF header truncated\n";
        return false;
    }

    uint16_t numSections = rdLE16(data.data() + coffOff + 2);
    uint16_t optHdrSize = rdLE16(data.data() + coffOff + 16);
    const size_t secTableOff = coffOff + 20 + static_cast<size_t>(optHdrSize);
    if (!hasRange(secTableOff, static_cast<size_t>(numSections) * 40u, data.size())) {
        err << "PE: section table truncated\n";
        return false;
    }

    size_t maxRawEnd = 0;
    for (uint16_t i = 0; i < numSections; ++i) {
        size_t secOff = secTableOff + static_cast<size_t>(i) * 40;
        uint32_t rawSize = rdLE32(data.data() + secOff + 16);
        uint32_t rawOff = rdLE32(data.data() + secOff + 20);
        uint64_t secEnd64 = static_cast<uint64_t>(rawOff) + static_cast<uint64_t>(rawSize);
        if (secEnd64 > static_cast<uint64_t>(data.size())) {
            err << "PE: section raw data extends past end of file\n";
            return false;
        }
        size_t secEnd = static_cast<size_t>(secEnd64);
        if (secEnd > maxRawEnd)
            maxRawEnd = secEnd;
    }

    overlayOff = maxRawEnd;
    return true;
}

// Check that every path in requiredPaths (after sanitization) is present in names.
// Used by the verifyXxxPayload family to assert the presence of critical files
// without re-parsing the archive structure.
bool requireArchivePaths(const std::set<std::string> &names,
                         const std::vector<std::string> &requiredPaths,
                         const char *kind,
                         std::ostream &err) {
    for (const auto &required : requiredPaths) {
        std::string clean;
        try {
            clean = sanitizePackageRelativePath(required, kind);
        } catch (const std::exception &ex) {
            err << kind << ": invalid required path '" << required << "': " << ex.what() << "\n";
            return false;
        }
        if (clean.empty()) {
            err << kind << ": required path must not be empty\n";
            return false;
        }
        if (names.find(clean) == names.end()) {
            err << kind << ": missing required payload path '" << clean << "'\n";
            return false;
        }
    }
    return true;
}

// Verify a ZIP archive and assert the presence of all requiredEntries.
// Combines verifyZip (structural check) with requireArchivePaths (payload check).
bool verifyZipPayload(const std::vector<uint8_t> &data,
                      const std::vector<std::string> &requiredEntries,
                      const char *kind,
                      std::ostream &err) {
    if (!verifyZip(data, err))
        return false;
    try {
        ZipReader reader(data.data(), data.size());
        std::set<std::string> names;
        for (const auto &entry : reader.entries())
            names.insert(sanitizePackageRelativePath(entry.name, kind));
        return requireArchivePaths(names, requiredEntries, kind, err);
    } catch (const std::exception &ex) {
        err << kind << ": " << ex.what() << "\n";
        return false;
    }
}

} // namespace

// ============================================================================
// ZIP Verification
// ============================================================================

bool verifyZip(const std::vector<uint8_t> &data, std::ostream &err) {
    try {
        ZipReader reader(data.data(), data.size());
        std::set<std::string> seen;
        for (const auto &entry : reader.entries()) {
            std::string clean;
            try {
                clean = sanitizePackageRelativePath(entry.name, "zip entry path");
            } catch (const std::exception &ex) {
                err << "ZIP: unsafe entry path '" << entry.name << "': " << ex.what() << "\n";
                return false;
            }
            if (clean.empty()) {
                err << "ZIP: empty entry path\n";
                return false;
            }
            if (!seen.insert(clean).second) {
                err << "ZIP: duplicate normalized entry '" << clean << "'\n";
                return false;
            }
            (void)reader.extract(entry);
        }
    } catch (const std::exception &ex) {
        err << ex.what() << "\n";
        return false;
    }
    return true;
}

bool verifyMacOSAppZip(const std::vector<uint8_t> &data,
                       const std::string &appBundleName,
                       const std::string &executableName,
                       std::ostream &err) {
    return verifyZipPayload(data,
                            {appBundleName + "/Contents/Info.plist",
                             appBundleName + "/Contents/PkgInfo",
                             appBundleName + "/Contents/MacOS/" + executableName},
                            "macOS app ZIP",
                            err);
}

// ============================================================================
// .deb (ar) Verification
// ============================================================================

// Full .deb (ar) structural verification: checks ar magic, expects exactly
// debian-binary / control.tar.gz / data.tar.gz members in that order,
// validates control.tar.gz contains a "control" entry, and verifies the USTAR
// structure of both tarballs. If outDataNames is non-null, fills it with
// normalized paths from data.tar.gz for payload checking.
bool verifyDebInternal(const std::vector<uint8_t> &data,
                       std::ostream &err,
                       std::set<std::string> *outDataNames) {
    // Check ar magic
    if (data.size() < 8) {
        err << "DEB: file too small (" << data.size() << " bytes)\n";
        return false;
    }

    if (std::memcmp(data.data(), "!<arch>\n", 8) != 0) {
        err << "DEB: missing ar magic '!<arch>\\n'\n";
        return false;
    }

    std::vector<uint8_t> controlTarGz;
    std::vector<uint8_t> dataTarGz;
    bool foundControl = false;
    bool foundData = false;
    size_t pos = 8; // after ar magic
    size_t memberIndex = 0;
    const char *expectedMembers[] = {"debian-binary", "control.tar.gz", "data.tar.gz"};
    while (pos + 60 <= data.size()) {
        // Check header terminator
        if (data[pos + 58] != '`' || data[pos + 59] != '\n') {
            err << "DEB: invalid member header terminator at offset " << pos << "\n";
            return false;
        }

        // Read name (16 bytes)
        std::string name(reinterpret_cast<const char *>(data.data() + pos), 16);
        while (!name.empty() && name.back() == ' ')
            name.pop_back();
        auto end = name.find('/');
        if (end != std::string::npos)
            name = name.substr(0, end);

        // Read size
        size_t sz = 0;
        if (!parseDecimalField(data.data() + pos + 48, 10, sz)) {
            err << "DEB: invalid member size field at offset " << pos << "\n";
            return false;
        }
        const size_t contentOff = pos + 60;
        if (sz > data.size() - contentOff) {
            err << "DEB: member '" << name << "' content truncated\n";
            return false;
        }
        if (memberIndex >= 3) {
            err << "DEB: unexpected extra ar member '" << name << "'\n";
            return false;
        }
        if (name != expectedMembers[memberIndex]) {
            err << "DEB: member " << memberIndex << " is '" << name << "', expected '"
                << expectedMembers[memberIndex] << "'\n";
            return false;
        }

        const uint8_t *content = data.data() + contentOff;
        if (memberIndex == 0) {
            if (sz != 4 || std::memcmp(content, "2.0\n", 4) != 0) {
                err << "DEB: debian-binary content is not exactly '2.0\\n'\n";
                return false;
            }
        } else if (memberIndex == 1) {
            if (foundControl) {
                err << "DEB: duplicate control.tar.gz member\n";
                return false;
            }
            foundControl = true;
            controlTarGz.assign(content, content + sz);
        } else if (memberIndex == 2) {
            if (foundData) {
                err << "DEB: duplicate data.tar.gz member\n";
                return false;
            }
            foundData = true;
            dataTarGz.assign(content, content + sz);
        }

        // Advance past header + data + optional padding
        pos += 60 + sz;
        if (sz % 2 != 0) {
            if (pos >= data.size()) {
                err << "DEB: missing ar padding byte after member '" << name << "'\n";
                return false;
            }
            if (data[pos] != '\n') {
                err << "DEB: invalid ar padding byte after member '" << name << "'\n";
                return false;
            }
            pos++; // odd-size padding
        }
        ++memberIndex;
    }

    if (pos != data.size()) {
        err << "DEB: trailing or truncated ar member data\n";
        return false;
    }
    if (memberIndex != 3) {
        err << "DEB: expected exactly debian-binary, control.tar.gz, and data.tar.gz members\n";
        return false;
    }
    if (!foundControl) {
        err << "DEB: 'control.tar.gz' member not found\n";
        return false;
    }
    if (!foundData) {
        err << "DEB: 'data.tar.gz' member not found\n";
        return false;
    }

    try {
        std::set<std::string> controlNames;
        const auto controlTar = gunzip(controlTarGz.data(), controlTarGz.size());
        if (!verifyTarBytes(controlTar, err, &controlNames))
            return false;
        if (controlNames.find("control") == controlNames.end()) {
            err << "DEB: control.tar.gz does not contain ./control\n";
            return false;
        }

        const auto dataTar = gunzip(dataTarGz.data(), dataTarGz.size());
        if (!verifyTarBytes(dataTar, err, outDataNames))
            return false;
    } catch (const std::exception &ex) {
        err << "DEB: compressed tar verification failed: " << ex.what() << "\n";
        return false;
    }

    return true;
}

bool verifyDeb(const std::vector<uint8_t> &data, std::ostream &err) {
    return verifyDebInternal(data, err, nullptr);
}

bool verifyDebPayload(const std::vector<uint8_t> &data,
                      const std::vector<std::string> &requiredPaths,
                      std::ostream &err) {
    std::set<std::string> dataNames;
    if (!verifyDebInternal(data, err, &dataNames))
        return false;
    return requireArchivePaths(dataNames, requiredPaths, "DEB", err);
}

bool verifyTarGz(const std::vector<uint8_t> &data, std::ostream &err) {
    try {
        const auto tarBytes = gunzip(data.data(), data.size());
        return verifyTarBytes(tarBytes, err);
    } catch (const std::exception &ex) {
        err << "TAR.GZ: " << ex.what() << "\n";
        return false;
    }
}

bool verifyTarGzPayload(const std::vector<uint8_t> &data,
                        const std::vector<std::string> &requiredPaths,
                        std::ostream &err) {
    try {
        std::set<std::string> names;
        const auto tarBytes = gunzip(data.data(), data.size());
        if (!verifyTarBytes(tarBytes, err, &names))
            return false;
        return requireArchivePaths(names, requiredPaths, "TAR.GZ", err);
    } catch (const std::exception &ex) {
        err << "TAR.GZ: " << ex.what() << "\n";
        return false;
    }
}

// ============================================================================
// PE Verification
// ============================================================================

bool verifyPE(const std::vector<uint8_t> &data, std::ostream &err) {
    if (data.size() < 64) {
        err << "PE: file too small (" << data.size() << " bytes)\n";
        return false;
    }

    // DOS header: "MZ" magic
    if (data[0] != 'M' || data[1] != 'Z') {
        err << "PE: missing DOS 'MZ' magic\n";
        return false;
    }

    // e_lfanew at offset 60: pointer to PE signature
    const size_t peOff = static_cast<size_t>(rdLE32(data.data() + 60));
    if (!hasRange(peOff, 4, data.size())) {
        err << "PE: e_lfanew (" << peOff << ") points past end of file\n";
        return false;
    }

    // PE signature: "PE\0\0"
    if (data[peOff] != 'P' || data[peOff + 1] != 'E' || data[peOff + 2] != 0 ||
        data[peOff + 3] != 0) {
        err << "PE: invalid PE signature at offset " << peOff << "\n";
        return false;
    }

    // COFF header at peOff+4
    const size_t coffOff = peOff + 4;
    if (!hasRange(coffOff, 20, data.size())) {
        err << "PE: COFF header truncated\n";
        return false;
    }

    uint16_t machine = rdLE16(data.data() + coffOff);
    if (machine != 0x8664 && machine != 0xAA64) {
        err << "PE: unexpected machine type 0x" << std::hex << machine << std::dec << "\n";
        return false;
    }

    uint16_t numSections = rdLE16(data.data() + coffOff + 2);
    uint16_t optHdrSize = rdLE16(data.data() + coffOff + 16);

    // Optional header
    const size_t optOff = coffOff + 20;
    if (!hasRange(optOff, optHdrSize, data.size())) {
        err << "PE: optional header truncated\n";
        return false;
    }

    // PE32+ magic = 0x020B
    uint16_t optMagic = rdLE16(data.data() + optOff);
    if (optMagic != 0x020B) {
        err << "PE: expected PE32+ magic 0x020B, got 0x" << std::hex << optMagic << std::dec
            << "\n";
        return false;
    }

    // Verify section headers don't overlap
    const size_t secTableOff = optOff + static_cast<size_t>(optHdrSize);

    struct SecInfo {
        uint32_t rawOff;
        uint32_t rawSize;
    };

    std::vector<SecInfo> sections;

    for (uint16_t i = 0; i < numSections; ++i) {
        const size_t hdrOff = secTableOff + static_cast<size_t>(i) * 40u;
        if (!hasRange(hdrOff, 40, data.size())) {
            err << "PE: section header " << i << " truncated\n";
            return false;
        }
        uint32_t rawSize = rdLE32(data.data() + hdrOff + 16);
        uint32_t rawOff = rdLE32(data.data() + hdrOff + 20);
        if (static_cast<uint64_t>(rawOff) + static_cast<uint64_t>(rawSize) >
            static_cast<uint64_t>(data.size())) {
            err << "PE: section raw data extends past end of file\n";
            return false;
        }
        if (rawSize > 0)
            sections.push_back({rawOff, rawSize});
    }

    // Check for overlap
    for (size_t i = 0; i < sections.size(); ++i) {
        for (size_t j = i + 1; j < sections.size(); ++j) {
            uint64_t aEnd =
                static_cast<uint64_t>(sections[i].rawOff) + static_cast<uint64_t>(sections[i].rawSize);
            uint64_t bEnd =
                static_cast<uint64_t>(sections[j].rawOff) + static_cast<uint64_t>(sections[j].rawSize);
            bool overlap = (sections[i].rawOff < bEnd && sections[j].rawOff < aEnd);
            if (overlap) {
                err << "PE: sections " << i << " and " << j << " overlap\n";
                return false;
            }
        }
    }

    return true;
}

bool verifyPEZipOverlay(const std::vector<uint8_t> &data, std::ostream &err) {
    if (!verifyPE(data, err))
        return false;

    size_t overlayOff = 0;
    if (!parsePeOverlayOffset(data, overlayOff, err))
        return false;

    if (overlayOff >= data.size()) {
        err << "PE: expected ZIP overlay after sections, but no overlay bytes were found\n";
        return false;
    }

    std::vector<uint8_t> overlay(data.begin() + overlayOff, data.end());
    if (!verifyZip(overlay, err)) {
        err << "PE: ZIP overlay verification failed\n";
        return false;
    }

    return true;
}

bool verifyPEZipOverlayPayload(const std::vector<uint8_t> &data,
                               const std::vector<std::string> &requiredEntries,
                               std::ostream &err) {
    if (!verifyPE(data, err))
        return false;

    size_t overlayOff = 0;
    if (!parsePeOverlayOffset(data, overlayOff, err))
        return false;

    if (overlayOff >= data.size()) {
        err << "PE: expected ZIP overlay after sections, but no overlay bytes were found\n";
        return false;
    }

    std::vector<uint8_t> overlay(data.begin() + overlayOff, data.end());
    if (!verifyZipPayload(overlay, requiredEntries, "PE ZIP overlay", err)) {
        err << "PE: ZIP overlay verification failed\n";
        return false;
    }

    return true;
}

} // namespace viper::pkg
