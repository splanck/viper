//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "il/transform/ConstFold.hpp"
#include "il/transform/DCE.hpp"
#include "il/transform/Mem2Reg.hpp"
#include "il/transform/PassManager.hpp"
#include "il/transform/Peephole.hpp"
#include "tools/common/module_loader.hpp"
#include "viper/il/IO.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
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
///          3. Instantiate the IL pass manager (pipelines O0/O1/O2 pre-registered)
///             and configure instrumentation hooks for printing/verifying.
///          4. Execute either a named pipeline or an explicit pass list and
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
    bool printBefore = false;
    bool printAfter = false;
    bool verifyEach = false;
    std::string pipelineName; // O0/O1/O2
    auto trimToken = [](const std::string &token)
    {
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
        std::string_view arg = argv[i];
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
        else if (arg == "--pipeline" && i + 1 < argc)
        {
            pipelineName = argv[++i];
        }
        else if (arg == "--mem2reg-stats")
        {
            mem2regStats = true;
        }
        else if (arg == "-print-before")
        {
            printBefore = true;
        }
        else if (arg == "-print-after")
        {
            printAfter = true;
        }
        else if (arg == "-verify-each")
        {
            verifyEach = true;
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
    pm.setInstrumentationStream(std::cerr);
    pm.setPrintBeforeEach(printBefore);
    pm.setPrintAfterEach(printAfter);
    if (verifyEach)
        pm.setVerifyBetweenPasses(true);

    // Ensure the canonical simplify-cfg registration (aggressive by default).
    pm.addSimplifyCFG();

    transform::PassManager::Pipeline selectedPipeline;
    auto resolvePipeline = [&](std::string name) -> bool
    {
        const auto *pipeline = pm.getPipeline(name);
        if (!pipeline)
        {
            std::cerr << "unknown pipeline '" << name << "' (use O0/O1/O2)\n";
            return false;
        }
        selectedPipeline = *pipeline;
        return true;
    };

    if (!pipelineName.empty())
    {
        std::string upper = pipelineName;
        std::transform(upper.begin(),
                       upper.end(),
                       upper.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (!resolvePipeline(upper))
            return 1;
    }

    if (passesExplicit)
    {
        selectedPipeline = passList;
    }
    else if (selectedPipeline.empty())
    {
        if (!resolvePipeline("O1"))
            return 1;
    }

    if (selectedPipeline.empty())
    {
        std::cerr << "no passes selected\n";
        return 1;
    }

    if (noMem2Reg)
    {
        selectedPipeline.erase(
            std::remove(selectedPipeline.begin(), selectedPipeline.end(), "mem2reg"),
            selectedPipeline.end());
    }
    // If mem2reg stats are requested, run mem2reg explicitly with stats and
    // remove it from the pass list to avoid double-running it. This preserves
    // the pipeline semantics while providing accurate statistics for the first
    // (and only) mem2reg run.
    if (mem2regStats)
    {
        selectedPipeline.erase(
            std::remove(selectedPipeline.begin(), selectedPipeline.end(), "mem2reg"),
            selectedPipeline.end());
        viper::passes::Mem2RegStats stats{};
        viper::passes::mem2reg(m, &stats);
        std::cout << "mem2reg: promoted " << stats.promotedVars << ", removed loads "
                  << stats.removedLoads << ", removed stores " << stats.removedStores << "\n";
    }

    for (const auto &passId : selectedPipeline)
    {
        if (!pm.passes().lookup(passId))
        {
            std::cerr << "unknown pass '" << passId << "'\n";
            return 1;
        }
    }

    pm.run(m, selectedPipeline);
    std::ofstream ofs(outFile);
    if (!ofs)
    {
        std::cerr << "unable to open " << outFile << "\n";
        return 1;
    }
    io::Serializer::write(m, ofs, io::Serializer::Mode::Canonical);
    return 0;
}
