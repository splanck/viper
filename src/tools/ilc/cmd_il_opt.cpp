//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/ilc/cmd_il_opt.cpp
// Purpose: Implement the `ilc il-opt` subcommand that loads IL, runs
//          transformations, and writes optimised output.
// Key invariants: Pass pipelines always contain only registered identifiers;
//                 instrumentation hooks mirror command-line flags; pass manager
//                 state is reset for each invocation so successive runs remain
//                 isolated.
// Ownership/Lifetime: Operates on caller-owned command-line buffers and local
//                     il::core::Module instances; no global state is mutated
//                     beyond diagnostic streams.
// Perf/Threading notes: Intended for single-shot CLI execution; pass execution
//                       complexity is dictated by the chosen pipeline.
// Links: docs/tools.md#ilc, docs/codemap.md#tools
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the optimisation pipeline entry point for `ilc`.
/// @details Demonstrates how to configure the pass manager, wire transformation
///          passes together via the public API, and honour user-specified
///          pipelines.

#include "cli.hpp"
#include "viper/il/IO.hpp"
#include "il/transform/ConstFold.hpp"
#include "il/transform/DCE.hpp"
#include "il/transform/Mem2Reg.hpp"
#include "il/transform/PassManager.hpp"
#include "il/transform/Peephole.hpp"
#include "tools/common/module_loader.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace il;

/// @brief Optimize an IL module using selected passes.
///
/// @details The driver orchestrates the `il-opt` workflow end-to-end:
///          1. Parse CLI options, enforcing `-o` for the output path, decoding
///             comma-separated `--passes` lists, and tracking feature toggles
///             such as `--no-mem2reg`, instrumentation prints, and verifier
///             hooks.
///          2. Load the input module via
///             @ref il::tools::common::loadModuleFromFile, emitting diagnostics
///             on failure and aborting early when the parse does not succeed.
///          3. Instantiate a @ref transform::PassManager, configure print/
///             verification hooks, register the standard transformation passes,
///             and seed a named default pipeline that mirrors the historical
///             optimisation sequence.
///          4. Resolve the actual pipeline to run (either the user-provided list
///             or the default, minus @c mem2reg when explicitly disabled), then
///             execute it over the module.
///          5. Serialize the resulting module to the requested file in canonical
///             form, surfacing file-system errors to stderr.
///          Any failure along the path returns 1 so the top-level driver can
///          report an error to the shell; otherwise the command exits with 0.
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

    pm.addSimplifyCFG();
    pm.registerModulePass("constfold",
                          [](core::Module &mod, transform::AnalysisManager &)
                          {
                              transform::constFold(mod);
                              return transform::PreservedAnalyses::none();
                          });
    pm.registerModulePass("peephole",
                          [](core::Module &mod, transform::AnalysisManager &)
                          {
                              transform::peephole(mod);
                              return transform::PreservedAnalyses::none();
                          });
    pm.registerModulePass("dce",
                          [](core::Module &mod, transform::AnalysisManager &)
                          {
                              transform::dce(mod);
                              return transform::PreservedAnalyses::none();
                          });
    pm.registerModulePass("mem2reg",
                          [mem2regStats](core::Module &mod, transform::AnalysisManager &)
                          {
                              viper::passes::Mem2RegStats st;
                              viper::passes::mem2reg(mod, mem2regStats ? &st : nullptr);
                              if (mem2regStats)
                              {
                                  std::cout << "mem2reg: promoted " << st.promotedVars
                                            << ", removed loads " << st.removedLoads
                                            << ", removed stores " << st.removedStores << "\n";
                              }
                              return transform::PreservedAnalyses::none();
                          });
    pm.addSimplifyCFG();
    pm.registerPipeline(
        "default", {"simplify-cfg", "mem2reg", "simplify-cfg", "constfold", "peephole", "dce"});
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
