//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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

#include "frontends/basic/ConstFolder.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"

namespace il::frontends::basic
{

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

    Parser parser(input.source, fileId, result.emitter.get());
    auto program = parser.parseProgram();
    if (!program)
    {
        return result;
    }

    foldConstants(*program);

    SemanticAnalyzer sema(*result.emitter);
    sema.analyze(*program);

    if (!result.succeeded())
    {
        return result;
    }

    Lowerer lower(options.boundsChecks);
    lower.setDiagnosticEmitter(result.emitter.get());
    result.module = lower.lower(*program);
    return result;
}

} // namespace il::frontends::basic
