// File: src/tools/ilc/cmd_il_opt.cpp
// Purpose: Implements IL optimization subcommand.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded modules.
// Links: docs/class-catalog.md

#include "Passes/Mem2Reg.h"
#include "cli.hpp"
#include "il/io/Parser.hpp"
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
    if (!passesExplicit)
    {
        passList = {"mem2reg", "constfold", "peephole", "dce"};
    }
    if (noMem2Reg)
    {
        passList.erase(std::remove(passList.begin(), passList.end(), "mem2reg"), passList.end());
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
    if (!io::Parser::parse(ifs, m, std::cerr))
        return 1;
    transform::PassManager pm;
    pm.addPass("constfold", transform::constFold);
    pm.addPass("peephole", transform::peephole);
    pm.addPass("dce", transform::dce);
    pm.addPass("mem2reg",
               [mem2regStats](core::Module &mod)
               {
                   viper::passes::Mem2RegStats st;
                   viper::passes::mem2reg(mod, mem2regStats ? &st : nullptr);
                   if (mem2regStats)
                   {
                       std::cout << "mem2reg: promoted " << st.promotedVars << ", removed loads "
                                 << st.removedLoads << ", removed stores " << st.removedStores
                                 << "\n";
                   }
               });
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
