//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/ArWriter.cpp
// Purpose: Write ar archives as used by the .deb package format.
//
// Key invariants:
//   - Global header: "!<arch>\n" (8 bytes).
//   - Per member header: 60 bytes, fields are space-padded ASCII.
//   - ar_fmag is always "`\n" (0x60, 0x0A).
//   - Data for odd-size members is followed by a '\n' padding byte.
//
// Ownership/Lifetime:
//   - Single-use writer, accumulates members then outputs.
//
// Links: ArWriter.hpp
//
//===----------------------------------------------------------------------===//

#include "ArWriter.hpp"
#include "PkgUtils.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>

namespace viper::pkg {

namespace {

/// @brief Add @p value to @p total while checking for size_t overflow.
/// @details The ar writer stores all output in memory. Guarding the pre-reserve estimate keeps
///          pathological member sets from wrapping the requested capacity before serialization.
void checkedAddEstimate(size_t &total, size_t value) {
    if (value > std::numeric_limits<size_t>::max() - total)
        throw std::runtime_error("ArWriter: archive size estimate overflow");
    total += value;
}

} // namespace

/// @brief Store one archive member. Copies `data` into an internal buffer along
/// with the name, mtime, and mode; the member is appended to the output on finish().
void ArWriter::addMember(
    const std::string &name, const uint8_t *data, size_t size, uint32_t mtime, uint32_t mode) {
    if (name.empty())
        throw std::runtime_error("ArWriter: member name must not be empty");
    if (name.size() > 15)
        throw std::runtime_error("ArWriter: member name is too long: " + name);
    for (unsigned char c : name) {
        if (c <= 0x20u || c == 0x7Fu || c == '/')
            throw std::runtime_error("ArWriter: invalid member name: " + name);
    }
    for (const auto &existing : members_) {
        if (existing.name == name)
            throw std::runtime_error("ArWriter: duplicate member name: " + name);
    }
    Member m;
    m.name = name;
    if (size != 0) {
        if (!data)
            throw std::runtime_error("ArWriter: null data pointer for non-empty member: " + name);
        m.data.assign(data, data + size);
    }
    m.mtime = mtime;
    m.mode = mode;
    members_.push_back(std::move(m));
}

/// @brief Convenience wrapper: converts the string to a byte span and delegates to addMember().
void ArWriter::addMemberString(const std::string &name,
                               const std::string &content,
                               uint32_t mtime,
                               uint32_t mode) {
    addMember(name, reinterpret_cast<const uint8_t *>(content.data()), content.size(), mtime, mode);
}

/// @brief Convenience wrapper: delegates to addMember() with the vector's data pointer and size.
void ArWriter::addMemberVec(const std::string &name,
                            const std::vector<uint8_t> &data,
                            uint32_t mtime,
                            uint32_t mode) {
    addMember(name, data.data(), data.size(), mtime, mode);
}

namespace {

/// @brief Write a right-space-padded ASCII field into a fixed-width ar header slot.
/// Throws if `value` is longer than `width`; the ar format has no overflow indicator so
/// silent truncation would produce a silently malformed archive.
void writeField(uint8_t *buf, const std::string &value, size_t width, const char *fieldName) {
    if (value.size() > width)
        throw std::runtime_error(std::string("ar ") + fieldName + " field too long: " + value);
    std::memcpy(buf, value.data(), value.size());
    std::memset(buf + value.size(), ' ', width - value.size());
}

} // namespace

/// @brief Serialize all accumulated members into a complete ar archive byte stream.
/// Emits the global "!<arch>\n" magic, then for each member writes the 60-byte
/// fixed-field header followed by the data and an optional '\n' pad byte for odd sizes.
std::vector<uint8_t> ArWriter::finish() const {
    std::vector<uint8_t> out;

    // Estimate size: 8 (magic) + per member (60 header + data + pad)
    size_t est = 8;
    for (const auto &m : members_) {
        checkedAddEstimate(est, 60);
        checkedAddEstimate(est, m.data.size());
        checkedAddEstimate(est, m.data.size() & 1);
    }
    out.reserve(est);

    // Global header
    const char *magic = "!<arch>\n";
    out.insert(out.end(), magic, magic + 8);

    for (const auto &m : members_) {
        uint8_t hdr[60];
        std::memset(hdr, ' ', 60);

        // ar_name[16]: "name/" padded with spaces
        std::string arName = m.name + "/";
        writeField(hdr + 0, arName, 16, "name");

        // ar_date[12]: decimal mtime
        writeField(hdr + 16, std::to_string(m.mtime), 12, "date");

        // ar_uid[6]: "0"
        writeField(hdr + 28, "0", 6, "uid");

        // ar_gid[6]: "0"
        writeField(hdr + 34, "0", 6, "gid");

        // ar_mode[8]: octal mode
        char modeStr[16];
        std::snprintf(modeStr, sizeof(modeStr), "%o", m.mode);
        writeField(hdr + 40, modeStr, 8, "mode");

        // ar_size[10]: decimal byte count
        writeField(hdr + 48, std::to_string(m.data.size()), 10, "size");

        // ar_fmag[2]: "`\n"
        hdr[58] = '`';
        hdr[59] = '\n';

        out.insert(out.end(), hdr, hdr + 60);
        out.insert(out.end(), m.data.begin(), m.data.end());

        // Pad to even size
        if (m.data.size() & 1)
            out.push_back('\n');
    }

    return out;
}

/// @brief Finalize the archive and write it atomically to `path`.
/// Calls finish() to build the byte stream, then delegates to the shared
/// same-directory temporary-file writer.
void ArWriter::finishToFile(const std::string &path) const {
    auto data = finish();
    writeFileAtomic(path, data);
}

} // namespace viper::pkg
