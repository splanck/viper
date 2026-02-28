//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/BasicCompiler.cpp
// Purpose: Implements the BASIC front-end driver responsible for parsing,
//          analysing, and lowering source programs into IL modules. The
//          translation unit stitches the individual pipeline stages together
//          while surfacing diagnostics to callers.
// Key invariants: Pipeline stages run in a strict order (parse → fold → sema →
//                 lower) and abort early on fatal diagnostics.
// Ownership/Lifetime: Diagnostic emitters and modules are owned by
//                     BasicCompilerResult; all other helpers are stack scoped.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief BASIC front-end compilation pipeline.
/// @details Provides the high-level entry point that runs the parser, constant
///          folder, semantic analyser, and lowerer in sequence.  Results are
///          returned as a @ref BasicCompilerResult containing diagnostics and the
///          generated module.

#include "frontends/basic/BasicCompiler.hpp"

#include "frontends/basic/AstPrinter.hpp"
#include "frontends/basic/ConstFolder.hpp"
#include "frontends/basic/Lexer.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Options.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "frontends/basic/passes/CollectProcs.hpp"
#include "viper/il/IO.hpp"
#include <iostream>

namespace il::frontends::basic
{

namespace
{
/// @brief Print every token from the BASIC source to stderr.
void dumpTokenStream(std::string_view source, uint32_t fileId)
{
    Lexer lexer(source, fileId);
    std::cerr << "=== BASIC Token Stream ===\n";
    for (;;)
    {
        Token tok = lexer.next();
        std::cerr << tok.loc.line << ':' << tok.loc.column << '\t' << tokenKindToString(tok.kind);
        if (!tok.lexeme.empty())
            std::cerr << "\t\"" << tok.lexeme << '"';
        std::cerr << '\n';
        if (tok.kind == TokenKind::EndOfFile)
            break;
    }
    std::cerr << "=== End Token Stream ===\n";
}
} // namespace

/// @brief Report whether the compilation pipeline produced a valid module.
///
/// @details The compiler front end records every diagnostic through the shared
///          emitter stored on the result.  Success therefore requires both an
///          initialised emitter (meaning the pipeline executed far enough to set
///          it up) and an empty error stream.  Downstream stages call this helper
///          before attempting to inspect or emit IL so malformed programs never
///          proceed to lowering.
///
/// @return True when compilation completed without recorded errors; otherwise
///         false.
[[nodiscard]] bool BasicCompilerResult::succeeded() const
{
    return emitter && emitter->errorCount() == 0;
}

/// @brief Compile BASIC source text into an IL module.
///
/// @details The pipeline performs the following steps:
///          1. Initialise a @ref DiagnosticEmitter that owns the diagnostic list
///             used by callers to inspect errors.
///          2. Ensure the input has an associated file identifier so diagnostics
///             can reference the correct source location.
///          3. Parse the BASIC program, aborting early if syntax errors are
///             detected.
///          4. Run constant folding to simplify obvious literal expressions
///             before semantic analysis.
///          5. Perform semantic analysis, recording any type or symbol issues.
///          6. When all checks succeed, lower the AST to IL using the Lowerer
///             helper and store the resulting module in the returned structure.
///
///          After each phase the intermediate result is validated so the caller
///          receives as much diagnostic information as possible without
///          attempting to emit IR from invalid input.
///
/// @param input   Compilation inputs including source buffer metadata.
/// @param options Pipeline configuration flags controlling lowering behaviour.
/// @param sm      Source manager used to register synthetic or disk-backed files.
/// @return Aggregated compilation result containing diagnostics and the module.
BasicCompilerResult compileBasic(const BasicCompilerInput &input,
                                 const BasicCompilerOptions &options,
                                 il::support::SourceManager &sm)
{
    BasicCompilerResult result{};
    result.emitter = std::make_unique<DiagnosticEmitter>(result.diagnostics, sm);

    uint32_t fileId = input.fileId.value_or(0);
    if (fileId == 0)
    {
        std::string path = input.path.empty() ? std::string{"<input>"} : std::string{input.path};
        fileId = sm.addFile(std::move(path));
    }
    result.fileId = fileId;

    if (fileId == 0)
    {
        result.emitter->emit(il::support::Severity::Error,
                             "B0005",
                             {},
                             0,
                             "source manager exhausted file identifier space");
        return result;
    }

    result.emitter->addSource(fileId, std::string{input.source});

    // Dump token stream before parsing if requested.
    if (options.dumpTokens)
    {
        dumpTokenStream(input.source, fileId);
    }

    // Runtime namespaces feature is controlled globally via FrontendOptions (default ON).
    // Allow disabling via environment variable for CLI/debugging.
    const char *ns_disable = std::getenv("VIPER_NO_RUNTIME_NAMESPACES");
    if (ns_disable && ns_disable[0] == '1')
        FrontendOptions::setEnableRuntimeNamespaces(false);
    else
        FrontendOptions::setEnableRuntimeNamespaces(true);

    // Prepare include stack for ADDFILE handling.
    std::vector<std::string> includeStack;
    Parser parser(
        input.source, fileId, result.emitter.get(), &sm, &includeStack, /*suppress*/ false);
    auto program = parser.parseProgram();
    if (!program)
    {
        return result;
    }

    // Dump AST after parsing.
    if (options.dumpAst)
    {
        AstPrinter printer;
        std::cerr << "=== AST after parsing ===\n" << printer.dump(*program) << "=== End AST ===\n";
    }

    // Post-parse: assign qualified names to procedures inside namespaces.
    // This enables semantic analysis to register nested procedures by their
    // fully-qualified names.
    CollectProcedures(*program);

    foldConstants(*program);

    SemanticAnalyzer sema(*result.emitter);
    sema.analyze(*program);

    if (!result.succeeded())
    {
        return result;
    }

    Lowerer lower(options.boundsChecks);
    lower.setDiagnosticEmitter(result.emitter.get());
    lower.setSemanticAnalyzer(&sema);
    result.module = lower.lower(*program);

    // Dump IL after lowering, before optimization.
    if (options.dumpIL)
    {
        std::cerr << "=== IL after lowering ===\n";
        io::Serializer::write(result.module, std::cerr);
        std::cerr << "=== End IL ===\n";
    }

    return result;
}

} // namespace il::frontends::basic
