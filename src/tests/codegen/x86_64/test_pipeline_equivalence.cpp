//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_pipeline_equivalence.cpp
// Purpose: Ensure the new pass-managed pipeline produces identical assembly to the direct backend.
// Key invariants: Assembly output must match byte-for-byte for a representative module.
// Ownership/Lifetime: Test constructs IL module objects locally; no file I/O.
// Links: src/codegen/x86_64/CodegenPipeline.cpp, src/codegen/x86_64/passes
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"
#include "codegen/x86_64/passes/EmitPass.hpp"
#include "codegen/x86_64/passes/LegalizePass.hpp"
#include "codegen/x86_64/passes/LoweringPass.hpp"
#include "codegen/x86_64/passes/PassManager.hpp"
#include "codegen/x86_64/passes/RegAllocPass.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace
{
[[nodiscard]] il::core::Instr makeRetConst(long long value)
{
    il::core::Instr instr;
    instr.op = il::core::Opcode::Ret;
    instr.type = il::core::Type(il::core::Type::Kind::Void);
    instr.operands.push_back(il::core::Value::constInt(value));
    return instr;
}

[[nodiscard]] il::core::Module makeSimpleModule()
{
    il::core::Module module{};

    il::core::Function fn;
    fn.name = "main";
    fn.retType = il::core::Type(il::core::Type::Kind::I64);

    il::core::BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;
    entry.instructions.push_back(makeRetConst(7));

    fn.blocks.push_back(entry);
    module.functions.push_back(fn);
    return module;
}

[[nodiscard]] viper::codegen::x64::CodegenResult baselineAssembly()
{
    viper::codegen::x64::passes::Module module{};
    module.il = makeSimpleModule();
    viper::codegen::x64::passes::Diagnostics diags{};
    viper::codegen::x64::passes::LoweringPass lowering{};
    if (!lowering.run(module, diags))
    {
        return {};
    }
    return viper::codegen::x64::emitModuleToAssembly(*module.lowered, {});
}

[[nodiscard]] viper::codegen::x64::CodegenResult managedAssembly()
{
    viper::codegen::x64::passes::Module module{};
    module.il = makeSimpleModule();
    viper::codegen::x64::passes::Diagnostics diags{};
    viper::codegen::x64::passes::PassManager manager{};
    manager.addPass(std::make_unique<viper::codegen::x64::passes::LoweringPass>());
    manager.addPass(std::make_unique<viper::codegen::x64::passes::LegalizePass>());
    manager.addPass(std::make_unique<viper::codegen::x64::passes::RegAllocPass>());
    manager.addPass(std::make_unique<viper::codegen::x64::passes::EmitPass>(
        viper::codegen::x64::CodegenOptions{}));

    if (!manager.run(module, diags) || !module.codegenResult)
    {
        return {};
    }
    return *module.codegenResult;
}

} // namespace

int main()
{
    const auto baseline = baselineAssembly();
    if (!baseline.errors.empty())
    {
        std::cerr << baseline.errors;
        return EXIT_FAILURE;
    }

    const auto managed = managedAssembly();
    if (!managed.errors.empty())
    {
        std::cerr << managed.errors;
        return EXIT_FAILURE;
    }

    if (baseline.asmText != managed.asmText)
    {
        std::cerr << "Assembly mismatch\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
