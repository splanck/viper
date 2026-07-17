//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/asset/AssetCompiler.hpp
// Purpose: Build-time asset compilation. Reads embed/pack directives from
//          ProjectConfig, resolves file paths, builds ZPAK blobs for .rodata
//          embedding, and writes standalone .zpak pack files.
//
// Key invariants:
//   - All source paths are resolved relative to ProjectConfig::rootDir.
//   - Directory entries are recursively enumerated.
//   - The embedded blob is a complete ZPAK archive (same format as .zpak files).
//   - Pack files are named <projectName>-<packName>.zpak.
//
// Ownership/Lifetime:
//   - AssetBundle owns its blob vector and pack file path list.
//   - Caller must keep ProjectConfig alive during compileAssets().
//
// Links: ZpakWriter.hpp (serialization), project_loader.hpp (input config)
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace il::tools::common {
struct ProjectConfig;
}

namespace zanna::asset {

/// @brief Result of asset compilation.
struct AssetBundle {
    /// ZPAK-format blob to embed in .rodata. Empty if no embed directives.
    std::vector<uint8_t> embeddedBlob;

    /// Paths to generated .zpak pack files on disk.
    std::vector<std::string> packFilePaths;

    /// Expected byte sizes for @ref packFilePaths entries.
    /// @details Cache validation uses these sizes with @ref packFileHashes to
    ///          detect truncated or externally modified pack files before
    ///          reusing a cached bundle.
    std::vector<std::uintmax_t> packFileSizes;

    /// Expected SHA-256 hashes for @ref packFilePaths entries.
    std::vector<std::string> packFileHashes;
};

/// @brief Compile assets declared in a project configuration.
///
/// 1. Resolves all `embed` entries → builds ZPAK blob for .rodata.
/// 2. Resolves all `pack` groups → writes .zpak files to outputDir.
///
/// @param config    Project configuration with embedAssets and packGroups.
/// @param outputDir Directory for .zpak output files.
/// @param err       Set to error message on failure.
/// @return AssetBundle on success, nullopt on failure.
std::optional<AssetBundle> compileAssets(const il::tools::common::ProjectConfig &config,
                                         const std::string &outputDir,
                                         std::string &err);

/// @brief Write a .o file containing the embedded blob as rodata symbols.
///
/// Uses Zanna's own ObjectFileWriter to produce a native .o file with:
///   zanna_asset_blob      → the ZPAK data bytes (.rodata)
///   zanna_asset_blob_size → uint64 size value (.rodata)
///
/// No external compiler is needed — fully self-contained.
///
/// @param blob     ZPAK-format blob data.
/// @param outPath  Path for the output .o file.
/// @param err      Set on failure.
/// @return true on success.
bool writeAssetBlobObject(const std::vector<uint8_t> &blob,
                          const std::string &outPath,
                          std::string &err);

} // namespace zanna::asset
