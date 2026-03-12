//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/BasicAnalysis.cpp
// Purpose: Implementation of partial BASIC compilation for IDE tooling.
// Key invariants:
//   - Pipeline runs parse → CollectProcedures → foldConstants → sema
//   - Stops before lowering
//   - Error-tolerant: continues analysis even on parse/sema errors
// Ownership/Lifetime:
//   - All returned data is fully owned by BasicAnalysisResult
// Links: frontends/basic/BasicAnalysis.hpp
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicAnalysis.hpp"

#include "frontends/basic/ConstFolder.hpp"
#include "frontends/basic/Options.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/passes/CollectProcs.hpp"

namespace il::frontends::basic
{

std::unique_ptr<BasicAnalysisResult> parseAndAnalyzeBasic(const BasicCompilerInput &input,
                                                          il::support::SourceManager &sm)
{
    auto result = std::make_unique<BasicAnalysisResult>();
    result->emitter = std::make_unique<DiagnosticEmitter>(result->diagnostics, sm);

    uint32_t fileId = input.fileId.value_or(0);
    if (fileId == 0)
    {
        std::string path = input.path.empty() ? std::string{"<input>"} : std::string{input.path};
        fileId = sm.addFile(std::move(path));
    }
    result->fileId = fileId;

    if (fileId == 0)
    {
        result->emitter->emit(il::support::Severity::Error,
                              "B0005",
                              {},
                              0,
                              "source manager exhausted file identifier space");
        return result;
    }

    result->emitter->addSource(fileId, std::string{input.source});

    // Enable runtime namespaces (same as compileBasic).
    const char *ns_disable = std::getenv("VIPER_NO_RUNTIME_NAMESPACES");
    if (ns_disable && ns_disable[0] == '1')
        FrontendOptions::setEnableRuntimeNamespaces(false);
    else
        FrontendOptions::setEnableRuntimeNamespaces(true);

    // Parse
    std::vector<std::string> includeStack;
    Parser parser(input.source, fileId, result->emitter.get(), &sm, &includeStack, false);
    auto program = parser.parseProgram();
    if (!program)
        return result;

    // Post-parse passes
    CollectProcedures(*program);
    foldConstants(*program);

    // Semantic analysis
    auto sema = std::make_unique<SemanticAnalyzer>(*result->emitter);
    sema->analyze(*program);

    result->ast = std::move(program);
    result->sema = std::move(sema);

    return result;
}

} // namespace il::frontends::basic
