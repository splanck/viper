//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/viperlang/Compiler.cpp
// Purpose: ViperLang compiler driver implementation.
//
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Compiler.hpp"
#include "frontends/viperlang/Lexer.hpp"
#include "frontends/viperlang/Lowerer.hpp"
#include "frontends/viperlang/Parser.hpp"
#include "frontends/viperlang/Sema.hpp"
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace il::frontends::viperlang
{

namespace
{

/// Resolve an import path to a file path relative to the importing file.
/// Supports both relative paths ("./foo" or "../bar") and simple names ("foo").
std::string resolveImportPath(const std::string &importPath, const std::string &importingFile)
{
    namespace fs = std::filesystem;

    // Get the directory of the importing file
    fs::path importingDir = fs::path(importingFile).parent_path();
    if (importingDir.empty())
    {
        importingDir = ".";
    }

    // If the import path starts with "./" or "../", treat it as relative
    if (importPath.starts_with("./") || importPath.starts_with("../"))
    {
        fs::path resolved = importingDir / importPath;
        // Add .viper extension if not present
        if (resolved.extension() != ".viper")
        {
            resolved += ".viper";
        }
        return resolved.lexically_normal().string();
    }

    // Otherwise, treat it as a simple name in the same directory
    fs::path resolved = importingDir / (importPath + ".viper");
    return resolved.lexically_normal().string();
}

/// Parse a single file and return its module AST.
std::unique_ptr<ModuleDecl> parseFile(const std::string &path,
                                      il::support::DiagnosticEngine &diag,
                                      il::support::SourceManager &sm)
{
    // Read file contents
    std::ifstream file(path);
    if (!file)
    {
        diag.report({il::support::Severity::Error,
                     "Failed to open imported file: " + path,
                     il::support::SourceLoc{},
                     ""});
        return nullptr;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    uint32_t fileId = sm.addFile(path);
    Lexer lexer(source, fileId, diag);
    Parser parser(lexer, diag);

    return parser.parseModule();
}

/// Maximum number of files that can be imported to prevent runaway compilation.
constexpr size_t kMaxImportedFiles = 100;

/// Recursively process imports for a module.
/// Returns false if any import fails.
bool processImports(ModuleDecl &module,
                    const std::string &modulePath,
                    std::set<std::string> &processedFiles,
                    il::support::DiagnosticEngine &diag,
                    il::support::SourceManager &sm,
                    size_t depth = 0)
{
    namespace fs = std::filesystem;

    // Safety guard: prevent excessive recursion depth
    if (depth > 50)
    {
        diag.report({il::support::Severity::Error,
                     "Import depth exceeds maximum (50). Check for circular imports.",
                     il::support::SourceLoc{},
                     ""});
        return false;
    }

    // Safety guard: prevent too many imported files
    if (processedFiles.size() > kMaxImportedFiles)
    {
        diag.report({il::support::Severity::Error,
                     "Too many imported files (>" + std::to_string(kMaxImportedFiles) +
                         "). Check for import cycles.",
                     il::support::SourceLoc{},
                     ""});
        return false;
    }

    // Mark this file as processed to avoid circular imports
    std::string normalizedPath = fs::absolute(modulePath).lexically_normal().string();
    if (processedFiles.count(normalizedPath) > 0)
    {
        return true; // Already processed
    }
    processedFiles.insert(normalizedPath);

    // Process each import
    for (const auto &import : module.imports)
    {
        std::string importFilePath = resolveImportPath(import.path, modulePath);

        // Check if already processed
        std::string normalizedImportPath = fs::absolute(importFilePath).lexically_normal().string();
        if (processedFiles.count(normalizedImportPath) > 0)
        {
            continue; // Skip already processed imports
        }

        // Parse the imported file
        auto importedModule = parseFile(importFilePath, diag, sm);
        if (!importedModule)
        {
            return false;
        }

        // Recursively process the imported module's imports first
        if (!processImports(*importedModule, importFilePath, processedFiles, diag, sm, depth + 1))
        {
            return false;
        }

        // Prepend the imported module's declarations to our module
        // This ensures imported definitions are processed before local code that calls them
        std::vector<DeclPtr> combined;
        combined.reserve(importedModule->declarations.size() + module.declarations.size());
        for (auto &decl : importedModule->declarations)
        {
            combined.push_back(std::move(decl));
        }
        for (auto &decl : module.declarations)
        {
            combined.push_back(std::move(decl));
        }
        module.declarations = std::move(combined);
    }

    return true;
}

} // anonymous namespace

bool CompilerResult::succeeded() const
{
    return diagnostics.errorCount() == 0;
}

CompilerResult compile(const CompilerInput &input,
                       const CompilerOptions & /*options*/,
                       il::support::SourceManager &sm)
{
    CompilerResult result{};

    // Register source file if not already registered
    if (input.fileId.has_value())
    {
        result.fileId = *input.fileId;
    }
    else
    {
        result.fileId = sm.addFile(std::string(input.path));
    }

    // Phase 1: Lexing
    Lexer lexer(std::string(input.source), result.fileId, result.diagnostics);

    // Phase 2: Parsing
    Parser parser(lexer, result.diagnostics);
    auto module = parser.parseModule();

    if (!module || parser.hasError())
    {
        // Parse failed, return with diagnostics
        return result;
    }

    // Phase 2.5: Process imports (load and merge imported files)
    if (!module->imports.empty())
    {
        std::set<std::string> processedFiles;
        std::string modulePath = std::string(input.path);
        if (!processImports(*module, modulePath, processedFiles, result.diagnostics, sm))
        {
            // Import processing failed
            return result;
        }
    }

    // Phase 3: Semantic Analysis
    Sema sema(result.diagnostics);
    bool semanticOk = sema.analyze(*module);

    if (!semanticOk)
    {
        // Semantic analysis failed, return with diagnostics
        return result;
    }

    // Phase 4: IL Lowering
    Lowerer lowerer(sema);
    result.module = lowerer.lower(*module);

    return result;
}

CompilerResult compileFile(const std::string &path,
                           const CompilerOptions &options,
                           il::support::SourceManager &sm)
{
    // Read file contents
    std::ifstream file(path);
    if (!file)
    {
        CompilerResult result{};
        result.diagnostics.report({il::support::Severity::Error,
                                   "Failed to open file: " + path,
                                   il::support::SourceLoc{},
                                   ""});
        return result;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    CompilerInput input;
    input.source = source;
    input.path = path;

    return compile(input, options, sm);
}

} // namespace il::frontends::viperlang
