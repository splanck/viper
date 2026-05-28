//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/XarWriter.cpp
// Purpose: Native XAR writer used by macOS flat installer packages.
//
//===----------------------------------------------------------------------===//

#include "XarWriter.hpp"

#include "PkgHash.hpp"
#include "PkgUtils.hpp"
#include "PkgZlib.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace viper::pkg {
namespace {

std::string normalizeXarPath(const std::string &path) {
    const std::string clean = sanitizePackageRelativePath(path, "xar file path");
    if (clean.empty())
        throw std::runtime_error("xar file path must not be empty or special");
    return clean;
}

std::string xmlEscape(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&apos;";
                break;
            default:
                out.push_back(ch);
                break;
        }
    }
    return out;
}

void appendBE16(std::vector<uint8_t> &out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xffu));
    out.push_back(static_cast<uint8_t>(value & 0xffu));
}

void appendBE32(std::vector<uint8_t> &out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xffu));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xffu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xffu));
    out.push_back(static_cast<uint8_t>(value & 0xffu));
}

void appendBE64(std::vector<uint8_t> &out, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8)
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xffu));
}

struct PreparedEntry {
    std::string path;
    std::vector<uint8_t> extracted;
    std::vector<uint8_t> archived;
    bool compressed{false};
    uint32_t mode{0644};
    uint64_t offset{0};
};

struct XarTreeNode {
    std::string name;
    std::string path;
    bool directory{true};
    uint32_t mode{0755};
    const PreparedEntry *file{nullptr};
    std::map<std::string, std::unique_ptr<XarTreeNode>> children;
};

std::vector<std::string> splitPathComponents(const std::string &path) {
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos <= path.size()) {
        const size_t next = path.find('/', pos);
        parts.push_back(next == std::string::npos ? path.substr(pos)
                                                  : path.substr(pos, next - pos));
        if (next == std::string::npos)
            break;
        pos = next + 1;
    }
    return parts;
}

XarTreeNode *ensureDirectoryNode(XarTreeNode &root, const std::string &path, uint32_t mode) {
    XarTreeNode *node = &root;
    std::string current;
    for (const std::string &part : splitPathComponents(path)) {
        if (!current.empty())
            current += "/";
        current += part;
        auto &child = node->children[part];
        if (!child) {
            child = std::make_unique<XarTreeNode>();
            child->name = part;
            child->path = current;
            child->directory = true;
            child->mode = 0755;
        }
        if (!child->directory)
            throw std::runtime_error("xar path component is already a file: " + current);
        node = child.get();
    }
    node->mode = mode & 07777u;
    return node;
}

void addFileNode(XarTreeNode &root, const PreparedEntry &entry) {
    const auto parts = splitPathComponents(entry.path);
    if (parts.empty())
        throw std::runtime_error("xar file path must not be empty");
    XarTreeNode *node = &root;
    std::string current;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        if (!current.empty())
            current += "/";
        current += parts[i];
        auto &child = node->children[parts[i]];
        if (!child) {
            child = std::make_unique<XarTreeNode>();
            child->name = parts[i];
            child->path = current;
            child->directory = true;
            child->mode = 0755;
        }
        if (!child->directory)
            throw std::runtime_error("xar path component is already a file: " + current);
        node = child.get();
    }
    auto &leaf = node->children[parts.back()];
    if (leaf)
        throw std::runtime_error("duplicate xar file path: " + entry.path);
    leaf = std::make_unique<XarTreeNode>();
    leaf->name = parts.back();
    leaf->path = entry.path;
    leaf->directory = false;
    leaf->mode = entry.mode;
    leaf->file = &entry;
}

void writeFileDataXml(std::ostringstream &toc, const PreparedEntry &entry, int depth) {
    const std::string indent(static_cast<size_t>(depth) * 1u, ' ');
    toc << indent << "<data>\n";
    toc << indent << " <archived-checksum style=\"sha1\">"
        << sha1Hex(entry.archived.data(), entry.archived.size()) << "</archived-checksum>\n";
    toc << indent << " <extracted-checksum style=\"sha1\">"
        << sha1Hex(entry.extracted.data(), entry.extracted.size()) << "</extracted-checksum>\n";
    toc << indent << " <encoding style=\""
        << (entry.compressed ? "application/x-gzip" : "application/octet-stream") << "\"/>\n";
    toc << indent << " <size>" << entry.extracted.size() << "</size>\n";
    toc << indent << " <offset>" << entry.offset << "</offset>\n";
    toc << indent << " <length>" << entry.archived.size() << "</length>\n";
    toc << indent << "</data>\n";
}

void writeTreeNodeXml(std::ostringstream &toc, const XarTreeNode &node, int &id, int depth) {
    const std::string indent(static_cast<size_t>(depth) * 1u, ' ');
    toc << indent << "<file id=\"" << id++ << "\">\n";
    toc << indent << " <name>" << xmlEscape(node.name) << "</name>\n";
    toc << indent << " <type>" << (node.directory ? "directory" : "file") << "</type>\n";
    toc << indent << " <mode>0" << std::oct << node.mode << std::dec << "</mode>\n";
    toc << indent << " <uid>0</uid>\n";
    toc << indent << " <user>root</user>\n";
    toc << indent << " <gid>0</gid>\n";
    toc << indent << " <group>wheel</group>\n";
    if (node.directory) {
        for (const auto &child : node.children)
            writeTreeNodeXml(toc, *child.second, id, depth + 1);
    } else {
        if (node.file == nullptr)
            throw std::runtime_error("xar file node is missing data: " + node.path);
        writeFileDataXml(toc, *node.file, depth + 1);
    }
    toc << indent << "</file>\n";
}

} // namespace

void XarWriter::addDirectory(const std::string &path, uint32_t mode) {
    const std::string clean = normalizeXarPath(path);
    if (!seenNames_.insert(clean).second)
        return;
    Entry entry;
    entry.kind = EntryKind::Directory;
    entry.path = clean;
    entry.mode = mode & 07777u;
    entries_.push_back(std::move(entry));
}

void XarWriter::addFile(
    const std::string &name, const uint8_t *data, size_t size, bool compress, uint32_t mode) {
    const std::string clean = normalizeXarPath(name);
    if (!seenNames_.insert(clean).second)
        throw std::runtime_error("duplicate xar file path: " + clean);
    Entry entry;
    entry.kind = EntryKind::File;
    entry.path = clean;
    if (size != 0) {
        if (data == nullptr)
            throw std::runtime_error("xar file data pointer is null: " + clean);
        entry.data.assign(data, data + size);
    }
    entry.compress = compress;
    entry.mode = mode & 07777u;
    entries_.push_back(std::move(entry));
}

void XarWriter::addFileVec(const std::string &name,
                           const std::vector<uint8_t> &data,
                           bool compress,
                           uint32_t mode) {
    addFile(name, data.data(), data.size(), compress, mode);
}

void XarWriter::addFileString(const std::string &name,
                              const std::string &content,
                              bool compress,
                              uint32_t mode) {
    addFile(
        name, reinterpret_cast<const uint8_t *>(content.data()), content.size(), compress, mode);
}

std::vector<uint8_t> XarWriter::finish() const {
    std::vector<PreparedEntry> prepared;
    prepared.reserve(entries_.size());
    uint64_t heapOffset = 20; // XAR TOC checksum bytes live at heap offset 0.
    for (const Entry &entry : entries_) {
        if (entry.kind == EntryKind::Directory)
            continue;
        PreparedEntry out;
        out.path = entry.path;
        out.extracted = entry.data;
        out.archived =
            entry.compress ? zlibCompress(entry.data.data(), entry.data.size()) : entry.data;
        out.compressed = entry.compress;
        out.mode = entry.mode;
        out.offset = heapOffset;
        heapOffset += out.archived.size();
        prepared.push_back(std::move(out));
    }

    XarTreeNode root;
    for (const Entry &entry : entries_) {
        if (entry.kind == EntryKind::Directory)
            ensureDirectoryNode(root, entry.path, entry.mode);
    }
    for (const PreparedEntry &entry : prepared)
        addFileNode(root, entry);

    std::ostringstream toc;
    toc << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    toc << "<xar>\n";
    toc << " <toc>\n";
    toc << "  <checksum style=\"sha1\">\n";
    toc << "   <size>20</size>\n";
    toc << "   <offset>0</offset>\n";
    toc << "  </checksum>\n";
    toc << "  <creation-time>1970-01-01T00:00:00Z</creation-time>\n";
    int id = 1;
    for (const auto &child : root.children)
        writeTreeNodeXml(toc, *child.second, id, 2);
    toc << " </toc>\n";
    toc << "</xar>\n";

    const std::string tocText = toc.str();
    const auto tocBytes = reinterpret_cast<const uint8_t *>(tocText.data());
    const auto tocCompressed = zlibCompress(tocBytes, tocText.size());
    const auto tocChecksum = sha1Bytes(tocCompressed.data(), tocCompressed.size());

    std::vector<uint8_t> out;
    out.reserve(28 + tocCompressed.size() + static_cast<size_t>(heapOffset));
    appendBE32(out, 0x78617221u); // "xar!"
    appendBE16(out, 28);
    appendBE16(out, 1);
    appendBE64(out, tocCompressed.size());
    appendBE64(out, tocText.size());
    appendBE32(out, 1); // SHA-1 TOC checksum
    out.insert(out.end(), tocCompressed.begin(), tocCompressed.end());
    out.insert(out.end(), tocChecksum.begin(), tocChecksum.end());
    for (const PreparedEntry &entry : prepared)
        out.insert(out.end(), entry.archived.begin(), entry.archived.end());
    return out;
}

void XarWriter::finishToFile(const std::string &path) const {
    auto data = finish();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot write xar archive: " + path);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
    if (!out)
        throw std::runtime_error("failed to write xar archive: " + path);
}

} // namespace viper::pkg
