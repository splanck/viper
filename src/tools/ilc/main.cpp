// File: src/tools/ilc/main.cpp
// Purpose: Main driver for IL compiler and runner.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded modules.
// Links: docs/class-catalog.md

#include "Passes/Mem2Reg.h"
#include "frontends/basic/ConstFolder.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "il/io/Parser.hpp"
#include "il/io/Serializer.hpp"
#include "il/transform/ConstFold.hpp"
#include "il/transform/DCE.hpp"
#include "il/transform/PassManager.hpp"
#include "il/transform/Peephole.hpp"
#include "il/verify/Verifier.hpp"
#include "support/source_manager.hpp"
#include "vm/VM.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace il;
using namespace il::frontends::basic;
using namespace il::support;

static void usage()
{
    std::cerr << "ilc v0.1.0\n"
              << "Usage: ilc -run <file.il> [--trace] [--stdin-from <file>] [--max-steps N]"
                 " [--bounds-checks]\n"
              << "       ilc front basic -emit-il <file.bas> [--bounds-checks]\n"
              << "       ilc front basic -run <file.bas> [--trace] [--stdin-from <file>] "
                 "[--max-steps N] [--bounds-checks]\n"
              << "       ilc il-opt <in.il> -o <out.il> --passes p1,p2\n";
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        usage();
        return 1;
    }

    std::string cmd = argv[1];
    bool trace = false;

    if (cmd == "-run")
    {
        if (argc < 3)
        {
            usage();
            return 1;
        }
        std::string ilFile = argv[2];
        std::string stdinPath;
        uint64_t maxSteps = 0;
        for (int i = 3; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == "--trace")
            {
                trace = true;
            }
            else if (arg == "--stdin-from" && i + 1 < argc)
            {
                stdinPath = argv[++i];
            }
            else if (arg == "--max-steps" && i + 1 < argc)
            {
                maxSteps = std::stoull(argv[++i]);
            }
            else if (arg == "--bounds-checks")
            {
            }
            else
            {
                usage();
                return 1;
            }
        }
        std::ifstream ifs(ilFile);
        if (!ifs)
        {
            std::cerr << "unable to open " << ilFile << "\n";
            return 1;
        }
        core::Module m;
        if (!io::Parser::parse(ifs, m, std::cerr))
            return 1;
        if (!verify::Verifier::verify(m, std::cerr))
            return 1;
        if (!stdinPath.empty())
        {
            if (!freopen(stdinPath.c_str(), "r", stdin))
            {
                std::cerr << "unable to open stdin file\n";
                return 1;
            }
        }
        vm::VM vm(m, trace, maxSteps);
        return static_cast<int>(vm.run());
    }

    if (cmd == "il-opt")
    {
        if (argc < 5)
        {
            usage();
            return 1;
        }
        std::string inFile = argv[2];
        std::string outFile;
        std::vector<std::string> passList;
        bool passesExplicit = false;
        bool noMem2Reg = false;
        bool mem2regStats = false;
        for (int i = 3; i < argc; ++i)
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
            passList.erase(std::remove(passList.begin(), passList.end(), "mem2reg"),
                           passList.end());
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
                           std::cout << "mem2reg: promoted " << st.promotedVars
                                     << ", removed loads " << st.removedLoads << ", removed stores "
                                     << st.removedStores << "\n";
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

    if (cmd == "front" && argc >= 3)
    {
        std::string lang = argv[2];
        if (lang == "basic")
        {
            bool emitIl = false;
            bool run = false;
            std::string file;
            std::string stdinPath;
            uint64_t maxSteps = 0;
            bool boundsChecks = false;
            for (int i = 3; i < argc; ++i)
            {
                std::string arg = argv[i];
                if (arg == "-emit-il" && i + 1 < argc)
                {
                    emitIl = true;
                    file = argv[++i];
                }
                else if (arg == "-run" && i + 1 < argc)
                {
                    run = true;
                    file = argv[++i];
                }
                else if (arg == "--trace")
                {
                    trace = true;
                }
                else if (arg == "--stdin-from" && i + 1 < argc)
                {
                    stdinPath = argv[++i];
                }
                else if (arg == "--max-steps" && i + 1 < argc)
                {
                    maxSteps = std::stoull(argv[++i]);
                }
                else if (arg == "--bounds-checks")
                {
                    boundsChecks = true;
                }
                else
                {
                    usage();
                    return 1;
                }
            }
            if ((emitIl == run) || file.empty())
            {
                usage();
                return 1;
            }

            std::ifstream in(file);
            if (!in)
            {
                std::cerr << "unable to open " << file << "\n";
                return 1;
            }
            std::ostringstream ss;
            ss << in.rdbuf();
            std::string src = ss.str();
            SourceManager sm;
            uint32_t fid = sm.addFile(file);
            Parser p(src, fid);
            auto prog = p.parseProgram();
            foldConstants(*prog);
            support::DiagnosticEngine de;
            DiagnosticEmitter em(de, sm);
            em.addSource(fid, src);
            SemanticAnalyzer sema(em);
            sema.analyze(*prog);
            if (em.errorCount() > 0)
            {
                em.printAll(std::cerr);
                return 1;
            }
            Lowerer lower(boundsChecks);
            core::Module m = lower.lower(*prog);

            if (emitIl)
            {
                io::Serializer::write(m, std::cout);
                return 0;
            }

            if (!verify::Verifier::verify(m, std::cerr))
                return 1;
            if (!stdinPath.empty())
            {
                if (!freopen(stdinPath.c_str(), "r", stdin))
                {
                    std::cerr << "unable to open stdin file\n";
                    return 1;
                }
            }
            vm::VM vm(m, trace, maxSteps);
            return static_cast<int>(vm.run());
        }
    }

    usage();
    return 1;
}
