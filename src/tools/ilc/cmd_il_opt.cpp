// File: src/tools/ilc/cmd_il_opt.cpp
// Purpose: Implements IL optimization subcommand.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded modules.
// Links: docs/codemap.md
// License: MIT.

#include "il/transform/Mem2Reg.hpp"
#include "cli.hpp"
#include "il/api/expected_api.hpp"
#include "il/io/Serializer.hpp"
#include "il/transform/ConstFold.hpp"
#include "il/transform/DCE.hpp"
#include "il/transform/PassManager.hpp"
#include "il/transform/Peephole.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace il;

/**
 * @brief Optimize an IL module using selected passes.
 * @details Treats the first argument as the input IL path and parses the
 *          remaining options to configure optimization. The `-o <file>` flag
 *          specifies the mandatory output file, `--passes a,b,c` selects an
 *          explicit comma-separated pipeline, `--no-mem2reg` filters the
 *          default pipeline, and `--mem2reg-stats` prints statistics when
 *          promotion runs. After parsing, the function loads the module from
 *          disk, registers the supported passes with a PassManager, executes
 *          the requested sequence, and serializes the canonical IL to the
 *          requested output path.
 *
 * @param argc Number of subcommand arguments (excluding `il-opt`).
 * @param argv Argument list starting with the input IL file.
 * @return Exit status code.
 */
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
                passList.push_back(passes.substr(pos, comma - pos));
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
    std::ifstream ifs(inFile);
    if (!ifs)
    {
        std::cerr << "unable to open " << inFile << "\n";
        return 1;
    }
    core::Module m;
    auto pe = il::api::v2::parse_text_expected(ifs, m);
    if (!pe)
    {
        il::support::printDiag(pe.error(), std::cerr);
        return 1;
    }
    transform::PassManager pm;
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
    pm.registerPipeline("default", {"mem2reg", "constfold", "peephole", "dce"});
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
