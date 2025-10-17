//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the `ilc il-opt` subcommand. The driver loads a module, configures
// a pass manager, and emits optimized IL according to user-selected pipelines.
// The helpers registered here showcase how to compose transformation passes from
// the public API.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the optimisation pipeline entry point for `ilc`.
/// @details The routine demonstrates how to configure the pass manager and wire
///          transformation passes together using the public API.

#include "cli.hpp"
#include "tools/common/module_loader.hpp"
#include "il/transform/Mem2Reg.hpp"
#include "il/io/Serializer.hpp"
#include "il/transform/ConstFold.hpp"
#include "il/transform/DCE.hpp"
#include "il/transform/PassManager.hpp"
#include "il/transform/Peephole.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace il;

/// @brief Optimize an IL module using selected passes.
///
/// @details Execution steps:
///          1. Parse subcommand options, requiring an output file via `-o` and
///             optionally collecting a custom `--passes` pipeline. Flags such as
///             `--no-mem2reg` and `--mem2reg-stats` tweak the default pipeline.
///          2. Load the input module from disk using
///             @ref il::tools::common::loadModuleFromFile.
///          3. Register transformation passes (`constfold`, `peephole`, `dce`,
///             `mem2reg`) with a @ref transform::PassManager, wiring lambdas
///             that call the public pass helpers.
///          4. Execute either the requested pipeline or the default sequence and
///             write the canonicalized IL to @p outFile.
///          The function returns zero on success or one when argument parsing,
///          file I/O, or pass execution fails.
///
/// @param argc Number of subcommand arguments (excluding `il-opt`).
/// @param argv Argument list starting with the input IL file.
/// @return Exit status code (zero on success, one on failure).
int cmdILOpt(int argc, char **argv)
{
    if (argc < 3)
    {
        usage();
        return 1;
    }
    std::string inFile = argv[0];
    std::string outFile;
    std::vector<std::string> passList;
    bool passesExplicit = false;
    bool noMem2Reg = false;
    bool mem2regStats = false;
    auto trimToken = [](const std::string &token) {
        auto begin = token.begin();
        auto end = token.end();
        while (begin != end && std::isspace(static_cast<unsigned char>(*begin)))
        {
            ++begin;
        }
        while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))))
        {
            --end;
        }
        return std::string(begin, end);
    };

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc)
        {
            outFile = argv[++i];
        }
        else if (arg == "--passes" && i + 1 < argc)
        {
            std::string passes = argv[++i];
            size_t pos = 0;
            passesExplicit = true;
            while (pos != std::string::npos)
            {
                size_t comma = passes.find(',', pos);
                std::string token = trimToken(passes.substr(pos, comma - pos));
                if (token.empty())
                {
                    usage();
                    return 1;
                }
                passList.push_back(std::move(token));
                if (comma == std::string::npos)
                    break;
                pos = comma + 1;
            }
        }
        else if (arg == "--no-mem2reg")
        {
            noMem2Reg = true;
        }
        else if (arg == "--mem2reg-stats")
        {
            mem2regStats = true;
        }
        else
        {
            usage();
            return 1;
        }
    }
    if (outFile.empty())
    {
        usage();
        return 1;
    }
    core::Module m;
    auto load = il::tools::common::loadModuleFromFile(inFile, m, std::cerr);
    if (!load.succeeded())
    {
        return 1;
    }
    transform::PassManager pm;
    pm.addSimplifyCFG();
    pm.registerModulePass(
        "constfold",
        [](core::Module &mod, transform::AnalysisManager &)
        {
            transform::constFold(mod);
            return transform::PreservedAnalyses::none();
        });
    pm.registerModulePass(
        "peephole",
        [](core::Module &mod, transform::AnalysisManager &)
        {
            transform::peephole(mod);
            return transform::PreservedAnalyses::none();
        });
    pm.registerModulePass(
        "dce",
        [](core::Module &mod, transform::AnalysisManager &)
        {
            transform::dce(mod);
            return transform::PreservedAnalyses::none();
        });
    pm.registerModulePass(
        "mem2reg",
        [mem2regStats](core::Module &mod, transform::AnalysisManager &)
        {
            viper::passes::Mem2RegStats st;
            viper::passes::mem2reg(mod, mem2regStats ? &st : nullptr);
            if (mem2regStats)
            {
                std::cout << "mem2reg: promoted " << st.promotedVars << ", removed loads " << st.removedLoads
                          << ", removed stores " << st.removedStores << "\n";
            }
            return transform::PreservedAnalyses::none();
        });
    pm.addSimplifyCFG();
    pm.registerPipeline(
        "default",
        {"simplify-cfg", "mem2reg", "simplify-cfg", "constfold", "peephole", "dce"});
    if (!passesExplicit)
    {
        if (const auto *pipeline = pm.getPipeline("default"))
            passList = *pipeline;
    }
    if (noMem2Reg)
    {
        passList.erase(std::remove(passList.begin(), passList.end(), "mem2reg"), passList.end());
    }
    pm.run(m, passList);
    std::ofstream ofs(outFile);
    if (!ofs)
    {
        std::cerr << "unable to open " << outFile << "\n";
        return 1;
    }
    io::Serializer::write(m, ofs, io::Serializer::Mode::Canonical);
    return 0;
}
