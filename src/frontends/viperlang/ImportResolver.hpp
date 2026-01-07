//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file ImportResolver.hpp
/// @brief Recursive import resolver for the ViperLang frontend.
///
/// @details Resolves and loads imported modules, detecting circular imports and
/// merging declarations into the importing module.
///
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/viperlang/AST.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace il::frontends::viperlang
{

/// @brief Resolves and merges ViperLang imports.
/// @details The resolver loads imported files recursively and prepends imported
///          declarations into the importing module, ensuring imported symbols
///          are available during semantic analysis and lowering.
class ImportResolver
{
  public:
    ImportResolver(il::support::DiagnosticEngine &diag, il::support::SourceManager &sm);

    /// @brief Resolve all imports for @p module.
    /// @param module The root module AST (already parsed).
    /// @param modulePath Path of the root module (used for relative imports).
    /// @return True if import processing succeeded, false if any import failed.
    bool resolve(ModuleDecl &module, const std::string &modulePath);

  private:
    static constexpr size_t kMaxImportDepth = 50;
    static constexpr size_t kMaxImportedFiles = 100;

    std::string resolveImportPath(const std::string &importPath,
                                  const std::string &importingFile) const;
    std::string normalizePath(const std::string &path) const;

    std::unique_ptr<ModuleDecl> parseFile(const std::string &path,
                                          il::support::SourceLoc importLoc);

    bool processModule(ModuleDecl &module,
                       const std::string &modulePath,
                       il::support::SourceLoc viaImportLoc,
                       size_t depth);

    void reportCycle(il::support::SourceLoc importLoc, const std::string &normalizedImportPath);

    il::support::DiagnosticEngine &diag_;
    il::support::SourceManager &sm_;
    std::set<std::string> processedFiles_;
    std::set<std::string> inProgressFiles_;
    std::vector<std::string> importStack_;
};

} // namespace il::frontends::viperlang
