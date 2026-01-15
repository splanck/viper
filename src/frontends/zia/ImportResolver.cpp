//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file ImportResolver.cpp
/// @brief Implementation of the Zia import resolver.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/ImportResolver.hpp"
#include "frontends/zia/Lexer.hpp"
#include "frontends/zia/Parser.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace il::frontends::zia
{

ImportResolver::ImportResolver(il::support::DiagnosticEngine &diag, il::support::SourceManager &sm)
    : diag_(diag), sm_(sm)
{
}

bool ImportResolver::resolve(ModuleDecl &module, const std::string &modulePath)
{
    processedFiles_.clear();
    inProgressFiles_.clear();
    importStack_.clear();
    return processModule(module, modulePath, il::support::SourceLoc{}, 0);
}

std::string ImportResolver::normalizePath(const std::string &path) const
{
    namespace fs = std::filesystem;
    return fs::absolute(fs::path(path)).lexically_normal().string();
}

std::string ImportResolver::resolveImportPath(const std::string &importPath,
                                              const std::string &importingFile) const
{
    namespace fs = std::filesystem;

    fs::path importingDir = fs::path(importingFile).parent_path();
    if (importingDir.empty())
        importingDir = ".";

    fs::path importP(importPath);
    fs::path resolved = importP.is_absolute() ? importP : (importingDir / importP);

    if (resolved.extension().empty())
        resolved += ".zia";

    return resolved.lexically_normal().string();
}

std::unique_ptr<ModuleDecl> ImportResolver::parseFile(const std::string &path,
                                                      il::support::SourceLoc importLoc)
{
    std::ifstream file(path);
    if (!file)
    {
        diag_.report({il::support::Severity::Error,
                      "Failed to open imported file: " + path,
                      importLoc,
                      "V1000"});
        return nullptr;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    uint32_t fileId = sm_.addFile(path);
    Lexer lexer(source, fileId, diag_);
    Parser parser(lexer, diag_);

    auto module = parser.parseModule();
    if (!module || parser.hasError())
        return nullptr;
    return module;
}

void ImportResolver::reportCycle(il::support::SourceLoc importLoc,
                                 const std::string &normalizedImportPath)
{
    std::string message = "Circular import detected";

    auto it = std::find(importStack_.begin(), importStack_.end(), normalizedImportPath);
    if (it != importStack_.end())
    {
        std::string chain;
        for (auto iter = it; iter != importStack_.end(); ++iter)
        {
            if (!chain.empty())
                chain += " -> ";
            chain += *iter;
        }
        if (!chain.empty())
            chain += " -> " + normalizedImportPath;
        message += ": " + chain;
    }

    diag_.report({il::support::Severity::Error, message, importLoc, "V1000"});
}

bool ImportResolver::processModule(ModuleDecl &module,
                                   const std::string &modulePath,
                                   il::support::SourceLoc viaImportLoc,
                                   size_t depth)
{
    if (depth > kMaxImportDepth)
    {
        diag_.report({il::support::Severity::Error,
                      "Import depth exceeds maximum (50). Check for circular imports.",
                      viaImportLoc,
                      "V1000"});
        return false;
    }

    if (processedFiles_.size() + inProgressFiles_.size() > kMaxImportedFiles)
    {
        diag_.report({il::support::Severity::Error,
                      "Too many imported files (>" + std::to_string(kMaxImportedFiles) +
                          "). Check for import cycles.",
                      viaImportLoc,
                      "V1000"});
        return false;
    }

    std::string normalizedPath = normalizePath(modulePath);
    if (processedFiles_.count(normalizedPath) != 0)
        return true;

    if (inProgressFiles_.count(normalizedPath) != 0)
    {
        reportCycle(viaImportLoc, normalizedPath);
        return false;
    }

    inProgressFiles_.insert(normalizedPath);
    importStack_.push_back(normalizedPath);

    // Collect all imported declarations first, then prepend them together.
    // This ensures proper dependency order: if A imports B then C, and C also
    // imports B (already processed), we get [B, C, A] not [C, B, A].
    std::vector<DeclPtr> importedDecls;

    for (const auto &bind : module.binds)
    {
        std::string bindFilePath = resolveImportPath(bind.path, modulePath);
        std::string normalizedBindPath = normalizePath(bindFilePath);

        if (processedFiles_.count(normalizedBindPath) != 0)
            continue;

        if (inProgressFiles_.count(normalizedBindPath) != 0)
        {
            reportCycle(bind.loc, normalizedBindPath);
            return false;
        }

        auto boundModule = parseFile(bindFilePath, bind.loc);
        if (!boundModule)
            return false;

        if (!processModule(*boundModule, bindFilePath, bind.loc, depth + 1))
            return false;

        // Collect this bind's declarations (which include its transitive binds)
        for (auto &decl : boundModule->declarations)
            importedDecls.push_back(std::move(decl));
    }

    // Now prepend all imported declarations before this module's declarations.
    // This maintains proper dependency order: imports come first in import order.
    if (!importedDecls.empty())
    {
        std::vector<DeclPtr> combined;
        combined.reserve(importedDecls.size() + module.declarations.size());
        for (auto &decl : importedDecls)
            combined.push_back(std::move(decl));
        for (auto &decl : module.declarations)
            combined.push_back(std::move(decl));
        module.declarations = std::move(combined);
    }

    importStack_.pop_back();
    inProgressFiles_.erase(normalizedPath);
    processedFiles_.insert(normalizedPath);
    return true;
}

} // namespace il::frontends::zia
