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
#include "tools/common/packaging/PkgHash.hpp"
#include "tools/common/packaging/PkgUtils.hpp"
#include "tools/common/project_loader.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace viper::asset {
namespace {
constexpr std::uintmax_t kMaxAssetFileBytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxAssetCacheEntries = 64;
constexpr std::size_t kMaxAssetFileCacheEntries = 128;
constexpr std::uintmax_t kMaxAssetFileCacheBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::uintmax_t kMaxAssetBundleCacheBytes = 512ULL * 1024ULL * 1024ULL;

/// @brief Cached contents of one validated asset file.
struct CachedAssetFile {
    std::uintmax_t size{0};     ///< File size at cache time.
    fs::file_time_type mtime{}; ///< Last-write time at cache time.
    std::string hash;           ///< SHA-256 hex digest of @ref data.
    std::vector<uint8_t> data;  ///< Complete file payload.
};

/// @brief Return a stable cache key for an asset path.
static std::string assetFileCacheKey(const fs::path &path) {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(path, ec);
    return (ec ? path.lexically_normal() : canonical).string();
}

/// @brief Shared cache for asset file payloads read during one process.
static std::unordered_map<std::string, std::shared_ptr<const CachedAssetFile>> &assetFileCache() {
    static std::unordered_map<std::string, std::shared_ptr<const CachedAssetFile>> cache;
    return cache;
}

/// @brief Return the total byte size currently retained by @ref assetFileCache.
static std::uintmax_t &assetFileCacheBytes() {
    static std::uintmax_t bytes = 0;
    return bytes;
}

/// @brief Mutex protecting @ref assetFileCache.
static std::mutex &assetFileCacheMutex() {
    static std::mutex mutex;
    return mutex;
}

/// @brief Remove one arbitrary cached asset payload when the cache reaches its cap.
static void evictOneAssetFileCacheEntry() {
    auto &cache = assetFileCache();
    if (!cache.empty()) {
        assetFileCacheBytes() -= cache.begin()->second->data.size();
        cache.erase(cache.begin());
    }
}

/// @brief Read and validate an asset file payload from disk.
static std::vector<uint8_t> readAssetFileUncached(const fs::path &path,
                                                  std::uintmax_t expectedSize) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read asset: " + path.string());
    std::vector<uint8_t> data(static_cast<size_t>(expectedSize));
    if (!data.empty())
        in.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!in)
        throw std::runtime_error("failed while reading asset: " + path.string());
    return data;
}

/// @brief Return cached asset bytes when size and mtime still match disk.
static std::shared_ptr<const CachedAssetFile> lookupCachedAssetFile(const fs::path &path,
                                                                    std::uintmax_t size,
                                                                    fs::file_time_type mtime) {
    std::lock_guard<std::mutex> lock(assetFileCacheMutex());
    auto it = assetFileCache().find(assetFileCacheKey(path));
    if (it == assetFileCache().end() || it->second->size != size || it->second->mtime != mtime)
        return {};
    return it->second;
}

/// @brief Store asset bytes and hash for later payload writing.
static void rememberCachedAssetFile(const fs::path &path,
                                    std::uintmax_t size,
                                    fs::file_time_type mtime,
                                    std::string hash,
                                    std::vector<uint8_t> data) {
    std::lock_guard<std::mutex> lock(assetFileCacheMutex());
    auto &cache = assetFileCache();
    const std::string key = assetFileCacheKey(path);
    if (auto old = cache.find(key); old != cache.end()) {
        assetFileCacheBytes() -= old->second->data.size();
        cache.erase(old);
    }
    while ((!cache.empty() && cache.size() >= kMaxAssetFileCacheEntries) ||
           (!cache.empty() && assetFileCacheBytes() + data.size() > kMaxAssetFileCacheBytes)) {
        evictOneAssetFileCacheEntry();
    }
    auto entry = std::make_shared<CachedAssetFile>(
        CachedAssetFile{size, mtime, std::move(hash), std::move(data)});
    assetFileCacheBytes() += entry->data.size();
    cache[key] = std::move(entry);
}

/// @brief Hash the full contents of @p path for cache invalidation.
/// @details Asset cache keys must change even when a filesystem preserves size and
///          coarse modification timestamps across an edit. The helper is used only
///          after source validation has enforced the asset size cap, so reading the
///          file here does not introduce an unbounded allocation.
static std::string contentHashForFile(const fs::path &path) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec)
        throw std::runtime_error("cannot stat asset for cache key: " + path.string());
    if (size > kMaxAssetFileBytes)
        throw std::runtime_error("asset file too large for cache key: " + path.string());
    const auto mtime = fs::last_write_time(path, ec);
    if (ec)
        throw std::runtime_error("cannot stat asset mtime for cache key: " + path.string());
    if (auto cached = lookupCachedAssetFile(path, size, mtime))
        return cached->hash;
    auto data = readAssetFileUncached(path, size);
    std::string hash = data.empty() ? viper::pkg::sha256Hex(nullptr, 0)
                                    : viper::pkg::sha256Hex(data.data(), data.size());
    rememberCachedAssetFile(path, size, mtime, hash, std::move(data));
    return hash;
}

/// @brief Append a single file's identity fingerprint to a cache key.
/// @details Folds the normalized path, byte size, and last-write-time ticks of
///          @p path into @p key (using "?" placeholders when stat calls fail), then
///          includes a SHA-256 content hash so edits with unchanged size/mtime still
///          invalidate the asset build cache.
/// @param path File to fingerprint.
/// @param key Cache-key string accumulated in place.
static void appendFileFingerprint(const fs::path &path, std::string &key) {
    std::error_code ec;
    key += path.lexically_normal().string();
    key.push_back('|');
    const auto size = fs::file_size(path, ec);
    key += ec ? std::string{"?"} : std::to_string(size);
    key.push_back('|');
    if (!ec) {
        ec.clear();
        const auto ticks = fs::last_write_time(path, ec).time_since_epoch().count();
        if (!ec)
            key += std::to_string(static_cast<long long>(ticks));
    }
    key.push_back('|');
    key += contentHashForFile(path);
    key.push_back('\n');
}

/// @brief Append a source directive's fingerprint (file or directory) to a key.
/// @details Resolves @p sourcePath against @p rootDir, prefixes the compression
///          mode ("C:"/"U:"), and fingerprints either the single file or, for a
///          directory, every regular file under it using the same safe traversal
///          helper as archive writing so the key and output observe identical
///          symlink/escape decisions.
/// @param rootDir Absolute project root used to resolve @p sourcePath.
/// @param sourcePath Project-relative source path from an embed/pack directive.
/// @param compressed Whether the entry is to be DEFLATE-compressed.
/// @param key Cache-key string accumulated in place.
static void appendSourceFingerprint(const fs::path &rootDir,
                                    const std::string &sourcePath,
                                    bool compressed,
                                    std::string &key) {
    fs::path absPath =
        viper::pkg::resolvePackageSourcePath(rootDir, sourcePath, "asset source path");
    key += compressed ? "C:" : "U:";
    std::error_code ec;
    if (fs::is_directory(absPath, ec)) {
        std::vector<fs::path> files;
        viper::pkg::safeDirectoryIterateResolved(
            absPath, rootDir, [&](const viper::pkg::SafeDirectoryEntry &entry) {
                if (entry.regularFile)
                    files.push_back(entry.resolvedPath);
            });
        std::sort(files.begin(), files.end(), [](const fs::path &a, const fs::path &b) {
            return a.generic_string() < b.generic_string();
        });
        for (const auto &file : files)
            appendFileFingerprint(file, key);
        return;
    }
    if (ec)
        throw std::runtime_error("cannot inspect asset source path '" + sourcePath +
                                 "': " + ec.message());
    appendFileFingerprint(absPath, key);
}

/// @brief Compute a content-addressed cache key for an asset compilation.
/// @details Combines the project root, output directory, and the fingerprints of
///          every embed entry and pack group (including pack name and
///          compression flag). Two calls with identical inputs and unchanged
///          files on disk produce the same key, allowing compileAssets() to
///          reuse a cached AssetBundle.
/// @param config Project configuration providing embed/pack directives.
/// @param outputDir Directory where pack files would be written.
/// @return A string uniquely identifying this asset build.
static std::string assetCacheKey(const il::tools::common::ProjectConfig &config,
                                 const std::string &outputDir) {
    fs::path rootDir(config.rootDir);
    std::string key = rootDir.lexically_normal().string() + "\n" + outputDir + "\n";
    for (const auto &entry : config.embedAssets)
        appendSourceFingerprint(rootDir, entry.sourcePath, false, key);
    for (const auto &group : config.packGroups) {
        key += "PACK:" + group.name + ":" + (group.compressed ? "1" : "0") + "\n";
        for (const auto &src : group.sources)
            appendSourceFingerprint(rootDir, src, group.compressed, key);
    }
    return key;
}

/// @brief Estimate retained memory for an AssetBundle stored in the process cache.
static std::uintmax_t retainedBytesForBundle(const AssetBundle &bundle) {
    std::uintmax_t bytes = bundle.embeddedBlob.size();
    for (const auto &path : bundle.packFilePaths)
        bytes += path.size();
    for (const auto &hash : bundle.packFileHashes)
        bytes += hash.size();
    bytes += bundle.packFileSizes.size() * sizeof(std::uintmax_t);
    return bytes;
}

/// @brief Return the total retained bytes for cached AssetBundle values.
static std::uintmax_t &assetBundleCacheBytes() {
    static std::uintmax_t bytes = 0;
    return bytes;
}

/// @brief Compute a SHA-256 hash for an already-generated pack file.
/// @details Pack files are expected to be much smaller than the process memory
///          limit enforced by asset validation. Reading here keeps cache
///          validation self-contained and catches same-size external rewrites.
static std::string hashGeneratedPackFile(const fs::path &path) {
    const auto data = viper::pkg::readFile(path);
    return data.empty() ? viper::pkg::sha256Hex(nullptr, 0)
                        : viper::pkg::sha256Hex(data.data(), data.size());
}

/// @brief Return true when asset compilation should print progress messages.
static bool assetVerboseEnabled() {
    const char *value = std::getenv("VIPER_ASSET_VERBOSE");
    return value && value[0] != '\0' && std::string_view(value) != "0";
}

} // namespace

// ─── File reading helper ────────────────────────────────────────────────────

/// @brief Read an entire file into a byte vector.
/// @return true on success; sets err on failure.
static bool readFile(const fs::path &path, std::vector<uint8_t> &out, std::string &err) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) {
        err = "cannot determine size of: " + path.string();
        return false;
    }
    if (size > kMaxAssetFileBytes) {
        err = "asset file too large: " + path.string() + " (limit: 256 MB)";
        return false;
    }
    const auto mtime = fs::last_write_time(path, ec);
    if (ec) {
        err = "cannot determine modification time of: " + path.string();
        return false;
    }
    if (auto cached = lookupCachedAssetFile(path, size, mtime)) {
        out = cached->data;
        return true;
    }
    try {
        out = readAssetFileUncached(path, size);
    } catch (const std::bad_alloc &) {
        err = "out of memory reading asset: " + path.string();
        return false;
    } catch (const std::exception &ex) {
        err = ex.what();
        return false;
    }
    std::string hash = out.empty() ? viper::pkg::sha256Hex(nullptr, 0)
                                   : viper::pkg::sha256Hex(out.data(), out.size());
    rememberCachedAssetFile(path, size, mtime, std::move(hash), out);
    return true;
}

// ─── Directory enumeration ──────────────────────────────────────────────────

/// @brief Collect all files under a directory recursively.
/// @param dir       Absolute path to directory.
/// @param rootDir   Project root for computing relative names.
/// @param entries   Output: pairs of (relative name, absolute path).
/// @param err       Set on error.
/// @return true on success.
static bool enumerateDir(const fs::path &dir,
                         const fs::path &rootDir,
                         std::vector<std::pair<std::string, fs::path>> &entries,
                         std::string &err) {
    try {
        viper::pkg::safeDirectoryIterateResolved(
            dir, rootDir, [&](const viper::pkg::SafeDirectoryEntry &entry) {
                if (!entry.regularFile)
                    return;
                std::error_code ec;
                fs::path relPath = fs::relative(entry.logicalPath, dir, ec);
                if (ec) {
                    throw std::runtime_error("cannot compute relative path for: " +
                                             entry.logicalPath.string());
                }
                entries.push_back({relPath.generic_string(), entry.resolvedPath});
            });
    } catch (const std::exception &e) {
        err = e.what();
        return false;
    }
    std::sort(entries.begin(), entries.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.first < rhs.first;
    });
    return true;
}

/// @brief Sanitize and resolve a project-relative asset source path.
/// @details Produces both the sanitized relative path (rejecting traversal/absolute
///          components) and the absolute on-disk path, throwing-to-error from the
///          packaging path helpers so callers receive a uniform failure string.
/// @param sourcePath Project-relative source path from a directive.
/// @param rootDir Absolute project root used for resolution.
/// @param resolvedPath Output: absolute resolved filesystem path.
/// @param cleanPath Output: sanitized relative path (forward-slash form).
/// @param err Set to the failure reason when resolution is rejected.
/// @return true on success; false with @p err set on rejection.
static bool resolveAssetSourcePath(const std::string &sourcePath,
                                   const fs::path &rootDir,
                                   fs::path &resolvedPath,
                                   std::string &cleanPath,
                                   std::string &err) {
    try {
        cleanPath = viper::pkg::sanitizePackageRelativePath(sourcePath, "asset source path");
        resolvedPath =
            viper::pkg::resolvePackageSourcePath(rootDir, sourcePath, "asset source path");
    } catch (const std::exception &e) {
        err = e.what();
        return false;
    }
    return true;
}

/// @brief Validate every embed entry and pack group before compilation.
/// @details Resolves each embed and pack source path (rejecting unsafe paths) and
///          validates each pack group name via normalizeExecName so failures are
///          reported up front rather than partway through writing output.
/// @param config Project configuration to validate.
/// @param err Set to the first validation failure encountered.
/// @return true when all sources and pack names are valid; false otherwise.
static bool validateAssetSources(const il::tools::common::ProjectConfig &config, std::string &err) {
    const fs::path rootDir(config.rootDir);
    fs::path resolvedPath;
    std::string cleanPath;
    for (const auto &entry : config.embedAssets) {
        if (!resolveAssetSourcePath(entry.sourcePath, rootDir, resolvedPath, cleanPath, err))
            return false;
    }
    for (const auto &group : config.packGroups) {
        try {
            (void)viper::pkg::normalizeExecName(group.name);
        } catch (const std::exception &e) {
            err = std::string("invalid asset pack name '") + group.name + "': " + e.what();
            return false;
        }
        for (const auto &src : group.sources) {
            if (!resolveAssetSourcePath(src, rootDir, resolvedPath, cleanPath, err))
                return false;
        }
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
static bool addSourceToWriter(const std::string &sourcePath,
                              const fs::path &rootDir,
                              VpaWriter &writer,
                              bool compress,
                              std::string &err) {
    fs::path absPath;
    std::string cleanPath;
    if (!resolveAssetSourcePath(sourcePath, rootDir, absPath, cleanPath, err))
        return false;

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
            try {
                writer.addEntry(name, data.data(), data.size(), compress);
            } catch (const std::exception &e) {
                err = e.what();
                return false;
            }
        }
    } else {
        // Single file.
        std::vector<uint8_t> data;
        if (!readFile(absPath, data, err))
            return false;
        // Use the sourcePath as the asset name (forward slashes).
        try {
            writer.addEntry(
                fs::path(cleanPath).generic_string(), data.data(), data.size(), compress);
        } catch (const std::exception &e) {
            err = e.what();
            return false;
        }
    }

    return true;
}

// ─── compileAssets ──────────────────────────────────────────────────────────

/// @brief Compile a project's embed/pack directives into an AssetBundle.
/// @details Validates all sources first, then consults a process-local cache
///          keyed by assetCacheKey() and guarded by a static mutex: a hit whose
///          pack files still exist on disk is returned without rebuilding. On a
///          miss, embed directives are gathered into a single in-memory VPA blob
///          and each pack group is written to `<outputDir>/<project>-<pack>.vpa`,
///          and the resulting bundle is cached before returning. See the header
///          for the parameter and return contract.
std::optional<AssetBundle> compileAssets(const il::tools::common::ProjectConfig &config,
                                         const std::string &outputDir,
                                         std::string &err) {
    if (!validateAssetSources(config, err))
        return std::nullopt;

    static std::mutex cacheMutex;
    static std::unordered_map<std::string, AssetBundle> cache;
    std::string cacheKey;
    try {
        cacheKey = assetCacheKey(config, outputDir);
    } catch (const std::exception &ex) {
        err = ex.what();
        return std::nullopt;
    }
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (auto it = cache.find(cacheKey); it != cache.end()) {
            bool packsExist = true;
            if (it->second.packFileSizes.size() != it->second.packFilePaths.size())
                packsExist = false;
            if (it->second.packFileHashes.size() != it->second.packFilePaths.size())
                packsExist = false;
            for (size_t i = 0; packsExist && i < it->second.packFilePaths.size(); ++i) {
                const auto &pack = it->second.packFilePaths[i];
                std::error_code ec;
                if (!fs::exists(pack, ec) || ec) {
                    packsExist = false;
                    break;
                }
                const auto size = fs::file_size(pack, ec);
                if (ec || size != it->second.packFileSizes[i]) {
                    packsExist = false;
                    break;
                }
                try {
                    if (hashGeneratedPackFile(pack) != it->second.packFileHashes[i]) {
                        packsExist = false;
                        break;
                    }
                } catch (const std::exception &) {
                    packsExist = false;
                    break;
                }
            }
            if (packsExist)
                return it->second;
        }
    }

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
            if (assetVerboseEnabled()) {
                std::cerr << "  embedded " << embedWriter.entryCount() << " asset(s) ("
                          << bundle.embeddedBlob.size() << " bytes)\n";
            }
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

        std::string safeProjectName;
        std::string safeGroupName;
        try {
            safeProjectName = viper::pkg::normalizeExecName(config.name);
            safeGroupName = viper::pkg::normalizeExecName(group.name);
        } catch (const std::exception &e) {
            err = e.what();
            return std::nullopt;
        }

        // Output path: <outputDir>/<projectName>-<packName>.vpa
        std::string vpaName = safeProjectName + "-" + safeGroupName + ".vpa";
        fs::path vpaPath = fs::path(outputDir) / vpaName;

        if (!packWriter.writeToFile(vpaPath.string(), err))
            return std::nullopt;

        bundle.packFilePaths.push_back(vpaPath.string());
        std::error_code sizeEc;
        const auto vpaSize = fs::file_size(vpaPath, sizeEc);
        if (sizeEc) {
            err = "cannot stat generated asset pack: " + vpaPath.string();
            return std::nullopt;
        }
        bundle.packFileSizes.push_back(vpaSize);
        try {
            bundle.packFileHashes.push_back(hashGeneratedPackFile(vpaPath));
        } catch (const std::exception &ex) {
            err = "cannot hash generated asset pack: " + std::string(ex.what());
            return std::nullopt;
        }
        if (assetVerboseEnabled())
            std::cerr << "  packed " << packWriter.entryCount() << " asset(s) into " << vpaName
                      << "\n";
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        const std::uintmax_t bundleBytes = retainedBytesForBundle(bundle);
        if (auto old = cache.find(cacheKey); old != cache.end()) {
            assetBundleCacheBytes() -= retainedBytesForBundle(old->second);
            cache.erase(old);
        }
        while (!cache.empty() &&
               (cache.size() >= kMaxAssetCacheEntries ||
                assetBundleCacheBytes() + bundleBytes > kMaxAssetBundleCacheBytes)) {
            auto victim =
                std::min_element(cache.begin(), cache.end(), [](const auto &lhs, const auto &rhs) {
                    return lhs.first < rhs.first;
                });
            if (victim != cache.end()) {
                assetBundleCacheBytes() -= retainedBytesForBundle(victim->second);
                cache.erase(victim);
            }
        }
        assetBundleCacheBytes() += bundleBytes;
        cache[cacheKey] = bundle;
    }
    return bundle;
}

// ─── writeAssetBlobObject ───────────────────────────────────────────────────

/// @brief Emit a native .o exposing the VPA blob as two .rodata symbols.
/// @details Builds an object file (via Viper's own ObjectFileWriter for the host
///          format/arch, so no external assembler is needed) containing an empty
///          .text and a .rodata section defining `viper_asset_blob` (the bytes)
///          and `viper_asset_blob_size` (a uint64 length). See the header for the
///          parameter and return contract.
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
