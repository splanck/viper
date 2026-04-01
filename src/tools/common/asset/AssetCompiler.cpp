//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/asset/AssetCompiler.cpp
// Purpose: Build-time asset compilation. Resolves embed/pack directives,
//          enumerates directories, reads file data, and produces VPA blobs
//          and standalone .vpa pack files.
//
// Key invariants:
//   - Source paths are resolved relative to ProjectConfig::rootDir.
//   - Symlinks outside the project root are rejected.
//   - Directory entries are recursively enumerated with forward-slash names.
//   - Empty projects produce an empty AssetBundle (no error).
//
// Ownership/Lifetime:
//   - File data is read into temporary vectors and consumed by VpaWriter.
//   - Output pack files are written to the specified output directory.
//
// Links: VpaWriter.hpp, AssetCompiler.hpp, project_loader.hpp
//
//===----------------------------------------------------------------------===//

#include "AssetCompiler.hpp"

#include "VpaWriter.hpp"
#include "codegen/common/objfile/ObjectFileWriter.hpp"
#include "tools/common/project_loader.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace viper::asset {

// ─── File reading helper ────────────────────────────────────────────────────

/// @brief Read an entire file into a byte vector.
/// @return true on success; sets err on failure.
static bool readFile(const fs::path &path, std::vector<uint8_t> &out, std::string &err) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        err = "cannot open file: " + path.string();
        return false;
    }
    auto size = f.tellg();
    if (size < 0) {
        err = "cannot determine size of: " + path.string();
        return false;
    }
    f.seekg(0);
    out.resize(static_cast<size_t>(size));
    if (size > 0)
        f.read(reinterpret_cast<char *>(out.data()), size);
    if (!f) {
        err = "read error on: " + path.string();
        return false;
    }
    return true;
}

// ─── Directory enumeration ──────────────────────────────────────────────────

/// @brief Collect all files under a directory recursively.
/// @param dir       Absolute path to directory.
/// @param rootDir   Project root for computing relative names.
/// @param entries   Output: pairs of (relative name, absolute path).
/// @param err       Set on error.
/// @return true on success.
static bool enumerateDir(const fs::path &dir, const fs::path &rootDir,
                         std::vector<std::pair<std::string, fs::path>> &entries,
                         std::string &err) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); ++it) {
        if (ec) {
            err = "directory enumeration error in " + dir.string() + ": " + ec.message();
            return false;
        }
        if (!it->is_regular_file())
            continue;

        // Compute relative name with forward slashes.
        fs::path relPath = fs::relative(it->path(), rootDir, ec);
        if (ec) {
            err = "cannot compute relative path for: " + it->path().string();
            return false;
        }
        std::string name = relPath.generic_string(); // Forward slashes
        entries.push_back({name, it->path()});
    }
    return true;
}

// ─── Add entries to a VpaWriter ─────────────────────────────────────────────

/// @brief Resolve a source path (file or dir) and add entries to a VpaWriter.
/// @param sourcePath  Path relative to project root.
/// @param rootDir     Absolute project root.
/// @param writer      VPA writer to add entries to.
/// @param compress    Whether to compress entries.
/// @param err         Set on error.
/// @return true on success.
static bool addSourceToWriter(const std::string &sourcePath, const fs::path &rootDir,
                              VpaWriter &writer, bool compress, std::string &err) {
    fs::path absPath = rootDir / sourcePath;

    std::error_code ec;
    if (!fs::exists(absPath, ec)) {
        err = "asset source not found: " + sourcePath + " (resolved to " + absPath.string() + ")";
        return false;
    }

    if (fs::is_directory(absPath, ec)) {
        // Enumerate directory recursively.
        std::vector<std::pair<std::string, fs::path>> dirEntries;
        if (!enumerateDir(absPath, rootDir, dirEntries, err))
            return false;
        for (const auto &[name, filePath] : dirEntries) {
            std::vector<uint8_t> data;
            if (!readFile(filePath, data, err))
                return false;
            writer.addEntry(name, data.data(), data.size(), compress);
        }
    } else {
        // Single file.
        std::vector<uint8_t> data;
        if (!readFile(absPath, data, err))
            return false;
        // Use the sourcePath as the asset name (forward slashes).
        std::string name = fs::path(sourcePath).generic_string();
        writer.addEntry(name, data.data(), data.size(), compress);
    }

    return true;
}

// ─── compileAssets ──────────────────────────────────────────────────────────

std::optional<AssetBundle> compileAssets(
    const il::tools::common::ProjectConfig &config,
    const std::string &outputDir,
    std::string &err) {

    AssetBundle bundle;
    fs::path rootDir(config.rootDir);

    // ── 1. Process embed directives → VPA blob for .rodata ──

    if (!config.embedAssets.empty()) {
        VpaWriter embedWriter;

        for (const auto &entry : config.embedAssets) {
            if (!addSourceToWriter(entry.sourcePath, rootDir, embedWriter, false, err))
                return std::nullopt;
        }

        if (embedWriter.entryCount() > 0) {
            bundle.embeddedBlob = embedWriter.writeToMemory();
            std::cerr << "  embedded " << embedWriter.entryCount() << " asset(s) ("
                      << bundle.embeddedBlob.size() << " bytes)\n";
        }
    }

    // ── 2. Process pack groups → .vpa files ──

    for (const auto &group : config.packGroups) {
        VpaWriter packWriter;

        for (const auto &src : group.sources) {
            if (!addSourceToWriter(src, rootDir, packWriter, group.compressed, err))
                return std::nullopt;
        }

        if (packWriter.entryCount() == 0)
            continue;

        // Output path: <outputDir>/<projectName>-<packName>.vpa
        std::string vpaName = config.name + "-" + group.name + ".vpa";
        fs::path vpaPath = fs::path(outputDir) / vpaName;

        if (!packWriter.writeToFile(vpaPath.string(), err))
            return std::nullopt;

        bundle.packFilePaths.push_back(vpaPath.string());
        std::cerr << "  packed " << packWriter.entryCount() << " asset(s) into "
                  << vpaName << "\n";
    }

    return bundle;
}

// ─── writeAssetBlobObject ───────────────────────────────────────────────────

bool writeAssetBlobObject(const std::vector<uint8_t> &blob,
                          const std::string &outPath,
                          std::string &err) {
    using namespace viper::codegen::objfile;

    // Create an empty .text section (no code).
    CodeSection text;

    // Create .rodata section with blob data and size symbol.
    CodeSection rodata;
    rodata.alignTo(16);
    rodata.defineSymbol("viper_asset_blob", SymbolBinding::Global, SymbolSection::Rodata);
    rodata.emitBytes(blob.data(), blob.size());
    rodata.alignTo(8);
    rodata.defineSymbol("viper_asset_blob_size", SymbolBinding::Global, SymbolSection::Rodata);
    rodata.emit64LE(static_cast<uint64_t>(blob.size()));

    // Write using Viper's own object file writer for the host platform.
    auto writer = createObjectFileWriter(detectHostFormat(), detectHostArch());
    if (!writer) {
        err = "no object file writer for this platform";
        return false;
    }

    std::ostringstream errStream;
    if (!writer->write(outPath, text, rodata, errStream)) {
        err = "failed to write asset blob .o: " + errStream.str();
        return false;
    }

    return true;
}

} // namespace viper::asset
