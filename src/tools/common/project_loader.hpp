//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/common/project_loader.hpp
// Purpose: Universal project system for Viper — discovers source files,
//          parses optional viper.project manifests, and resolves project
//          configuration for both Zia and BASIC frontends.
// Key invariants: ProjectConfig always has a valid entryFile and lang after
//                 successful resolution.
// Ownership/Lifetime: Caller owns the returned ProjectConfig.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support/diag_expected.hpp"
#include "tools/common/packaging/PackageConfig.hpp"

#include <string>
#include <vector>

namespace il::tools::common {

/// @brief Detected language for a project.
enum class ProjectLang {
    Zia,
    Basic,
    Mixed ///< Both .zia and .bas files; requires IL linker.
};

/// @brief Parsed project manifest or convention-inferred configuration.
/// @invariant After successful resolution, entryFile is non-empty and points
///            to an existing source file.
struct ProjectConfig {
    /// @brief Project name (from manifest or directory name).
    std::string name;

    /// @brief Project version string.
    std::string version{"0.0.0"};

    /// @brief Detected or declared language.
    ProjectLang lang{ProjectLang::Zia};

    /// @brief Absolute path to the project root directory.
    std::string rootDir;

    /// @brief Path to the entry point file (absolute).
    std::string entryFile;

    /// @brief All discovered source files (absolute paths).
    std::vector<std::string> sourceFiles;

    /// @brief Zia source files (populated for Mixed projects).
    std::vector<std::string> ziaFiles;

    /// @brief BASIC source files (populated for Mixed projects).
    std::vector<std::string> basicFiles;

    /// @brief Optimization level string ("O0", "O1", "O2").
    std::string optimizeLevel{"O0"};

    /// @brief Enable runtime bounds checks.
    bool boundsChecks{true};

    /// @brief Enable arithmetic overflow checks (Zia only).
    bool overflowChecks{true};

    /// @brief Enable null dereference checks (Zia only).
    bool nullChecks{true};

    /// @brief Package configuration (from package-* directives).
    viper::pkg::PackageConfig packageConfig;

    // ── Asset embedding ─────────────────────────────────────────────────

    /// @brief An asset path to embed directly into the executable.
    struct EmbedEntry {
        std::string sourcePath; ///< File or directory, relative to project root.
    };

    /// @brief A named group of assets to pack into a .vpa file.
    struct PackGroup {
        std::string name;                 ///< Pack name → produces <name>.vpa.
        std::vector<std::string> sources; ///< File/dir paths relative to project root.
        bool compressed{false};           ///< DEFLATE compress entries.
    };

    /// @brief Assets to embed in the executable's .rodata section.
    std::vector<EmbedEntry> embedAssets;

    /// @brief Named groups of assets to pack into .vpa files.
    std::vector<PackGroup> packGroups;
};

/// @brief Resolve a project from a CLI target path.
///
/// The target may be:
/// - A single .zia or .bas file -> single-file mode
/// - A directory -> convention or manifest mode
/// - A viper.project file path -> explicit manifest mode
///
/// @param target CLI argument (file or directory path).
/// @return ProjectConfig on success, diagnostic on failure.
il::support::Expected<ProjectConfig> resolveProject(const std::string &target);

/// @brief Parse a viper.project manifest file.
/// @param manifestPath Absolute path to the viper.project file.
/// @return ProjectConfig on success, diagnostic on failure.
il::support::Expected<ProjectConfig> parseManifest(const std::string &manifestPath);

} // namespace il::tools::common
