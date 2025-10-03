// File: src/frontends/basic/BasicCompiler.cpp
// Purpose: Implements the BASIC front-end pipeline producing IL modules.
// Key invariants: Diagnostics capture all failures; lowering only runs on valid programs.
// Ownership/Lifetime: Result owns diagnostics; borrows SourceManager for source mapping.
// Links: docs/codemap.md

#include "frontends/basic/BasicCompiler.hpp"

#include "frontends/basic/ConstFolder.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"

namespace il::frontends::basic
{

[[nodiscard]] bool BasicCompilerResult::succeeded() const
{
    return emitter && emitter->errorCount() == 0;
}

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
