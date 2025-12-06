//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the Pascal compiler driver. Currently a skeleton that emits
// a hard-coded "Hello" IL module to prove the plumbing works.
//
// TODO: Integrate actual Lexer, Parser, SemanticAnalyzer, and Lowerer.
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Compiler.hpp"
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

    // TODO: Implement actual compilation pipeline:
    //   1. Lexer: tokenize input.source
    //   2. Parser: build AST from tokens
    //   3. SemanticAnalyzer: type check and validate AST
    //   4. Lowerer: convert AST to IL

    // For now, emit a hard-coded skeleton IL module to prove wiring works.
    // This module prints a hello message and returns 0 from main.

    il::build::IRBuilder builder(result.module);

    // Declare external runtime function: rt_print_str(str) -> void
    builder.addExtern("rt_print_str",
                      il::core::Type(il::core::Type::Kind::Void),
                      {il::core::Type(il::core::Type::Kind::Str)});

    // Create @main function: i64 @main()
    il::core::Function &mainFn = builder.startFunction(
        "main",
        il::core::Type(il::core::Type::Kind::I64),
        {});

    // Create entry basic block
    il::core::BasicBlock &entry = builder.addBlock(mainFn, "entry");
    builder.setInsertPoint(entry);

    // Create global string constant
    const std::string helloMessage = "Hello from Viper Pascal frontend skeleton!";
    const std::string strLabel = "str.hello";
    builder.addGlobalStr(strLabel, helloMessage);

    // Emit const_str instruction to load the global string into a temporary
    il::core::Value strVal = builder.emitConstStr(strLabel, il::support::SourceLoc{});

    // Call rt_print_str with the string value
    builder.emitCall("rt_print_str",
                     {strVal},
                     std::nullopt,
                     il::support::SourceLoc{});

    // Return 0 from main
    builder.emitRet(il::core::Value::constInt(0), il::support::SourceLoc{});

    return result;
}

} // namespace il::frontends::pascal
