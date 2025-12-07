//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the Pascal compiler driver that integrates the lexer, parser,
// semantic analyzer, and IL lowerer for complete compilation pipeline.
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Compiler.hpp"
#include "frontends/pascal/Lexer.hpp"
#include "frontends/pascal/Lowerer.hpp"
#include "frontends/pascal/Parser.hpp"
#include "frontends/pascal/SemanticAnalyzer.hpp"
#include "il/build/IRBuilder.hpp"
#include "viper/il/Module.hpp"

namespace il::frontends::pascal
{

bool PascalCompilerResult::succeeded() const
{
    return diagnostics.errorCount() == 0;
}

PascalCompilerResult compilePascal(const PascalCompilerInput &input,
                                   const PascalCompilerOptions & /*options*/,
                                   il::support::SourceManager &sm)
{
    PascalCompilerResult result{};

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
    auto program = parser.parseProgram();

    if (!program || parser.hasError())
    {
        // Parse failed, return with diagnostics
        return result;
    }

    // Phase 3: Semantic Analysis
    SemanticAnalyzer analyzer(result.diagnostics);
    bool semanticOk = analyzer.analyze(*program);

    if (!semanticOk)
    {
        // Semantic analysis failed, return with diagnostics
        return result;
    }

    // Phase 4: IL Lowering
    Lowerer lowerer;
    result.module = lowerer.lower(*program, analyzer);

    return result;
}

PascalCompilerResult compilePascalMultiFile(const PascalMultiFileInput &input,
                                            const PascalCompilerOptions & /*options*/,
                                            il::support::SourceManager &sm)
{
    PascalCompilerResult result{};

    // Create shared semantic analyzer to accumulate unit exports
    SemanticAnalyzer analyzer(result.diagnostics);

    // Collect parsed units
    std::vector<std::unique_ptr<Unit>> parsedUnits;

    // Phase 1: Parse and analyze all units (in dependency order)
    for (const auto &unitInput : input.units)
    {
        uint32_t fileId;
        if (unitInput.fileId.has_value())
        {
            fileId = *unitInput.fileId;
        }
        else
        {
            fileId = sm.addFile(std::string(unitInput.path));
        }

        Lexer lexer(std::string(unitInput.source), fileId, result.diagnostics);
        Parser parser(lexer, result.diagnostics);
        auto [prog, unit] = parser.parse();

        if (parser.hasError())
            return result;

        if (unit)
        {
            // Analyze unit (this registers its exports)
            if (!analyzer.analyze(*unit))
                return result;

            parsedUnits.push_back(std::move(unit));
        }
    }

    // Phase 2: Parse and analyze the main program
    if (input.program.fileId.has_value())
    {
        result.fileId = *input.program.fileId;
    }
    else
    {
        result.fileId = sm.addFile(std::string(input.program.path));
    }

    Lexer programLexer(std::string(input.program.source), result.fileId, result.diagnostics);
    Parser programParser(programLexer, result.diagnostics);
    auto program = programParser.parseProgram();

    if (!program || programParser.hasError())
        return result;

    if (!analyzer.analyze(*program))
        return result;

    // Phase 3: Lower all units and program into combined module
    Lowerer lowerer;

    // Lower program first (creates @main)
    result.module = lowerer.lower(*program, analyzer);

    // Merge in each unit's functions
    for (auto &unit : parsedUnits)
    {
        Lowerer unitLowerer;
        auto unitModule = unitLowerer.lower(*unit, analyzer);
        Lowerer::mergeModule(result.module, unitModule);
    }

    return result;
}

} // namespace il::frontends::pascal
