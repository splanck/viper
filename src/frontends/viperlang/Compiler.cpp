//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Compiler.cpp
/// @brief Implementation of ViperLang compiler driver.
///
/// @details This file implements the compile() and compileFile() functions
/// that orchestrate the complete compilation pipeline. Key implementation:
///
/// ## Import Resolution
///
/// The processImports() function recursively resolves imports:
/// 1. Resolves import paths relative to the importing file
/// 2. Parses each imported file
/// 3. Recursively processes that file's imports
/// 4. Prepends imported declarations to the importing module
///
/// Import path resolution:
/// - "./foo" or "../bar" → Relative to importing file
/// - "foo" → Same directory as importing file, add .viper extension
///
/// ## Safety Guards
///
/// To prevent runaway compilation:
/// - Maximum import depth: 50 levels
/// - Maximum imported files: 100
/// - Circular import detection via processedFiles set
///
/// ## Compilation Phases
///
/// The compile() function executes phases in order:
/// 1. Create Lexer from source
/// 2. Parse with Parser to get AST
/// 3. Process imports (load and merge)
/// 4. Semantic analysis with Sema
/// 5. Lower to IL with Lowerer
///
/// @see Compiler.hpp for the public API
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Compiler.hpp"
#include "frontends/viperlang/ImportResolver.hpp"
#include "frontends/viperlang/Lexer.hpp"
#include "frontends/viperlang/Lowerer.hpp"
#include "frontends/viperlang/Parser.hpp"
#include "frontends/viperlang/Sema.hpp"
#include <fstream>
#include <sstream>

namespace il::frontends::viperlang
{

bool CompilerResult::succeeded() const
{
    return diagnostics.errorCount() == 0;
}

CompilerResult compile(const CompilerInput &input,
                       const CompilerOptions &options,
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
        ImportResolver resolver(result.diagnostics, sm);
        if (!resolver.resolve(*module, std::string(input.path)))
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
    Lowerer lowerer(sema, options);
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
                                   "V1000"});
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
