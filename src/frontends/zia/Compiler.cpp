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
#include "frontends/zia/ZiaAnalysis.hpp"
#include "frontends/zia/ZiaAstPrinter.hpp"
#include "il/transform/PassManager.hpp"
#include "viper/il/IO.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

namespace il::frontends::zia
{

namespace
{
/// @brief Print every token from the source to stderr.
/// @details Creates a fresh lexer and iterates until EOF, printing each token
///          with its location, kind, text, and literal values.
void dumpTokenStream(const std::string &source,
                     uint32_t fileId,
                     il::support::DiagnosticEngine &diag)
{
    Lexer lexer(source, fileId, diag);
    std::cerr << "=== Zia Token Stream ===\n";
    for (;;)
    {
        Token tok = lexer.next();
        std::cerr << tok.loc.line << ':' << tok.loc.column << '\t'
                  << tokenKindToString(tok.kind);
        if (!tok.text.empty())
            std::cerr << "\t\"" << tok.text << '"';
        if (tok.kind == TokenKind::IntegerLiteral)
            std::cerr << "\tvalue=" << tok.intValue;
        else if (tok.kind == TokenKind::NumberLiteral)
            std::cerr << "\tvalue=" << tok.floatValue;
        std::cerr << '\n';
        if (tok.kind == TokenKind::Eof)
            break;
    }
    std::cerr << "=== End Token Stream ===\n";
}
} // namespace

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

    // Phase 0 (optional): Token stream dump — uses a separate lexer so parsing
    // still works from the original one.
    if (options.dumpTokens)
    {
        dumpTokenStream(std::string(input.source), result.fileId, result.diagnostics);
    }

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

    // Dump AST after parsing (before sema).
    if (options.dumpAst)
    {
        ZiaAstPrinter printer;
        std::cerr << "=== AST after parsing ===\n"
                  << printer.dump(*module)
                  << "=== End AST ===\n";
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
    sema.initWarnings(options.warningPolicy, input.source);
    bool semanticOk = sema.analyze(*module);

    // Dump AST after semantic analysis.
    if (options.dumpSemaAst)
    {
        ZiaAstPrinter printer;
        std::cerr << "=== AST after semantic analysis ===\n"
                  << printer.dump(*module)
                  << "=== End AST ===\n";
    }

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

    // Dump IL after lowering, before optimization.
    if (options.dumpIL)
    {
        std::cerr << "=== IL after lowering ===\n";
        io::Serializer::write(result.module, std::cerr);
        std::cerr << "=== End IL ===\n";
    }

    // Phase 5: IL Optimization — use the canonical registered pipelines.
    // O1 and O2 pipelines are defined in PassManager's constructor and include
    // the full sequence of passes (SCCP, LICM, loop transforms, inlining, etc.).
    if (options.optLevel != OptLevel::O0)
    {
        il::transform::PassManager pm;
        pm.setVerifyBetweenPasses(false);

        // Enable per-pass IL dumps when requested.
        if (options.dumpILPasses)
        {
            pm.setPrintBeforeEach(true);
            pm.setPrintAfterEach(true);
            pm.setInstrumentationStream(std::cerr);
        }

        const std::string pipelineId = (options.optLevel == OptLevel::O2) ? "O2" : "O1";
        pm.runPipeline(result.module, pipelineId);
    }

    // Dump IL after the full optimization pipeline.
    if (options.dumpILOpt)
    {
        const char *level = (options.optLevel == OptLevel::O2) ? "O2"
                          : (options.optLevel == OptLevel::O1) ? "O1"
                                                               : "O0";
        std::cerr << "=== IL after optimization (" << level << ") ===\n";
        io::Serializer::write(result.module, std::cerr);
        std::cerr << "=== End IL ===\n";
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

std::unique_ptr<AnalysisResult> parseAndAnalyze(const CompilerInput &input,
                                                const CompilerOptions &options,
                                                il::support::SourceManager &sm)
{
    // Heap-allocate the result so DiagnosticEngine has a stable address.
    // Sema holds a reference to it; moving a unique_ptr never relocates the
    // pointed-to object, so the reference remains valid for the object's lifetime.
    auto result = std::make_unique<AnalysisResult>();

    // Register source file (matches the logic in compile()).
    uint32_t fileId =
        input.fileId.has_value() ? *input.fileId : sm.addFile(std::string(input.path));

    // Phase 1: Lexing
    Lexer lexer(std::string(input.source), fileId, result->diagnostics);

    // Phase 2: Parsing — continue on errors for tolerance.
    // Parser::parseModule() accumulates errors in result->diagnostics and
    // attempts to return a partial AST via resync-after-error recovery.
    Parser parser(lexer, result->diagnostics);
    auto module = parser.parseModule();

    if (!module)
    {
        // Complete parse failure — no AST to analyze.
        return result;
    }

    result->ast = std::move(module);

    // Phase 2.5: Import resolution (best-effort).
    // Failures are accumulated in diagnostics but do not abort analysis.
    if (!result->ast->binds.empty())
    {
        ImportResolver resolver(result->diagnostics, sm);
        resolver.resolve(*result->ast, std::string(input.path));
    }

    // Phase 3: Semantic analysis.
    // We always construct and run Sema — even when there were parse errors —
    // because partial type resolution is still valuable for completions.
    // DiagnosticEngine address is stable (heap-allocated in result).
    result->sema = std::make_unique<Sema>(result->diagnostics);
    result->sema->analyze(*result->ast);
    // Ignore false return: partial Sema state is the desired output.

    return result;
}

} // namespace il::frontends::zia
