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
#include <fstream>
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

    // Phase 5: IL Optimization
    if (options.optLevel != OptLevel::O0)
    {
        il::transform::PassManager pm;
        // Note: Verification between passes is disabled because passes like Mem2Reg
        // create new temporaries that don't have proper type information set.
        // The verifier now handles block-order-independent IL correctly, but
        // inter-pass verification needs passes to maintain complete type info.
        pm.setVerifyBetweenPasses(false);

        // Use optimized pipeline based on optimization level.
        // Note: SCCP is still disabled due to infinite loop on certain CFG patterns.
        if (options.optLevel == OptLevel::O2)
        {
            // Aggressive optimization pipeline with CFG simplification
            // Note: SimplifyCFG is run only once at the start due to issues
            // with running it after other passes that modify temp definitions.
            il::transform::PassManager::Pipeline pipeline = {
                "simplify-cfg", "mem2reg", "dce", "licm",
                "gvn", "earlycse", "dse", "peephole", "dce"};
            pm.run(result.module, pipeline);
        }
        else
        {
            // Standard optimization (O1): core passes for good performance
            // Note: SimplifyCFG is run only once at the start. Running it again
            // after other passes can cause issues where temps become undefined
            // due to block transformations (tracked as a future improvement).
            il::transform::PassManager::Pipeline pipeline = {
                "simplify-cfg", "mem2reg", "peephole", "dce"};
            pm.run(result.module, pipeline);
        }
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
