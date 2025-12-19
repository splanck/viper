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
#include <fstream>
#include <sstream>

namespace il::frontends::viperlang
{

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
                                   il::support::SourceLoc{}, ""});
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
