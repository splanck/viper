//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Compiler.cpp
/// @brief Implementation of Zia compiler driver.
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
/// - "foo" → Same directory as importing file, add .zia extension
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

#include "frontends/zia/Compiler.hpp"
#include "frontends/zia/ImportResolver.hpp"
#include "frontends/zia/Lexer.hpp"
#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/Parser.hpp"
#include "frontends/zia/Sema.hpp"
#include "il/transform/PassManager.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

namespace il::frontends::zia
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

    // Debug timing
    auto debugTime = [](const char *phase)
    {
        if (const char *env = std::getenv("ZIA_DEBUG_COMPILE"))
            std::cerr << "[zia] " << phase << std::endl;
    };

    debugTime("Phase 1: Lexing");
    // Phase 1: Lexing
    Lexer lexer(std::string(input.source), result.fileId, result.diagnostics);

    debugTime("Phase 2: Parsing");
    // Phase 2: Parsing
    Parser parser(lexer, result.diagnostics);
    auto module = parser.parseModule();

    if (!module || parser.hasError())
    {
        // Parse failed, return with diagnostics
        return result;
    }

    debugTime("Phase 2.5: Import resolution");
    // Phase 2.5: Process binds (load and merge bound files)
    if (!module->binds.empty())
    {
        ImportResolver resolver(result.diagnostics, sm);
        if (!resolver.resolve(*module, std::string(input.path)))
        {
            // Import processing failed
            return result;
        }
    }

    debugTime("Phase 3: Semantic Analysis");
    // Phase 3: Semantic Analysis
    Sema sema(result.diagnostics);
    bool semanticOk = sema.analyze(*module);

    if (!semanticOk)
    {
        // Semantic analysis failed, return with diagnostics
        return result;
    }

    debugTime("Phase 4: IL Lowering");
    // Phase 4: IL Lowering
    Lowerer lowerer(sema, result.diagnostics, options);
    result.module = lowerer.lower(*module);
    debugTime("Phase 4: Done");

    // Phase 5: IL Optimization — use the canonical registered pipelines.
    // O1 and O2 pipelines are defined in PassManager's constructor and include
    // the full sequence of passes (SCCP, LICM, loop transforms, inlining, etc.).
    if (options.optLevel != OptLevel::O0)
    {
        il::transform::PassManager pm;
        pm.setVerifyBetweenPasses(false);
        const std::string pipelineId = (options.optLevel == OptLevel::O2) ? "O2" : "O1";
        pm.runPipeline(result.module, pipelineId);
    }

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

} // namespace il::frontends::zia
