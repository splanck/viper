//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/ilc/cmd_codegen_arm64.cpp
// Purpose: Minimal CLI glue for `ilc codegen arm64 -S` using AArch64 AsmEmitter.
//
//===----------------------------------------------------------------------===//

#include "cmd_codegen_arm64.hpp"

#include "codegen/aarch64/AsmEmitter.hpp"
#include "codegen/aarch64/LowerILToMIR.hpp"
#include "codegen/common/ArgNormalize.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "tools/common/module_loader.hpp"

#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace viper::tools::ilc
{
namespace
{

using il::core::Opcode;

// No local condForOpcode; mapping lives in LowerILToMIR.

// Small helper that encapsulates the pattern-based lowering used by the arm64 CLI.
// Keeps cmd driver tidy and centralizes opcode/sequence mapping.
// (Pattern-lowering moved to LowerILToMIR)

constexpr std::string_view kUsage = "usage: ilc codegen arm64 <file.il> -S <file.s>\n";

struct ArgvView
{
    int argc;
    char **argv;

    [[nodiscard]] bool empty() const
    {
        return argc <= 0 || argv == nullptr;
    }

    [[nodiscard]] std::string_view front() const
    {
        return empty() ? std::string_view{} : argv[0];
    }

    [[nodiscard]] std::string_view at(int i) const
    {
        if (i < 0 || i >= argc || argv == nullptr)
            return std::string_view{};
        return argv[i];
    }

    [[nodiscard]] ArgvView drop_front(int n = 1) const
    {
        if (n >= argc)
            return {0, nullptr};
        return {argc - n, argv + n};
    }
};

struct Options
{
    std::string input_il;
    std::optional<std::string> output_s;
};

std::optional<Options> parseArgs(const ArgvView &args)
{
    if (args.empty())
    {
        std::cerr << kUsage;
        return std::nullopt;
    }
    Options opts;
    opts.input_il = std::string(args.front());
    for (int i = 1; i < args.argc; ++i)
    {
        const std::string_view tok = args.at(i);
        if (tok == "-S")
        {
            if (i + 1 >= args.argc)
            {
                std::cerr << "error: -S requires an output path\n" << kUsage;
                return std::nullopt;
            }
            opts.output_s = std::string(args.at(++i));
            continue;
        }
        std::cerr << "error: unknown flag '" << tok << "'\n" << kUsage;
        return std::nullopt;
    }
    if (!opts.output_s)
    {
        std::cerr << "error: -S is required for arm64 backend\n" << kUsage;
        return std::nullopt;
    }
    return opts;
}

int emitAssembly(const Options &opts)
{
    il::core::Module mod;
    const auto load = il::tools::common::loadModuleFromFile(opts.input_il, mod, std::cerr);
    if (!load.succeeded())
    {
        return 1;
    }

    // Open output stream
    std::ofstream out(*opts.output_s);
    if (!out)
    {
        std::cerr << "unable to open " << *opts.output_s << "\n";
        return 1;
    }

    using namespace viper::codegen::aarch64;
    auto &ti = darwinTarget();
    AsmEmitter emitter{ti};
    LowerILToMIR lowerer{ti};
    for (const auto &fn : mod.functions)
    {
        MFunction mir = lowerer.lowerFunction(fn);
        emitter.emitFunction(out, mir);
        out << "\n";
    }
    return 0;
}

// condForOpcode mapping moved to LowerILToMIR.

} // namespace

int cmd_codegen_arm64(int argc, char **argv)
{
    const ArgvView args{argc, argv};
    auto parsed = parseArgs(args);
    if (!parsed)
    {
        return 1;
    }
    return emitAssembly(*parsed);
}

} // namespace viper::tools::ilc
