// File: src/tools/ilc/cmd_run_il.cpp
// Purpose: Implements execution of IL programs.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded modules.
// Links: docs/class-catalog.md

#include "VM/Debug.h"
#include "VM/DebugScript.h"
#include "VM/Trace.h"
#include "cli.hpp"
#include "il/io/Parser.hpp"
#include "il/verify/Verifier.hpp"
#include "vm/VM.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

using namespace il;

/**
 * @brief Run an IL program from file.
 *
 * @param argc Number of subcommand arguments (excluding `-run`).
 * @param argv Argument list starting with the IL file path.
 * @return Exit status code.
 */
int cmdRunIL(int argc, char **argv)
{
    if (argc < 1)
    {
        usage();
        return 1;
    }
    std::string ilFile = argv[0];
    vm::TraceConfig traceCfg{};
    std::string stdinPath;
    uint64_t maxSteps = 0;
    vm::DebugCtrl dbg;
    std::unique_ptr<vm::DebugScript> script;
    bool stepFlag = false;
    bool continueFlag = false;
    bool countFlag = false;
    bool timeFlag = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--trace" || arg == "--trace=il")
        {
            traceCfg.mode = vm::TraceConfig::IL;
        }
        else if (arg == "--trace=src")
        {
            traceCfg.mode = vm::TraceConfig::SRC;
        }
        else if (arg == "--stdin-from" && i + 1 < argc)
        {
            stdinPath = argv[++i];
        }
        else if (arg == "--max-steps" && i + 1 < argc)
        {
            maxSteps = std::stoull(argv[++i]);
        }
        else if (arg == "--break" && i + 1 < argc)
        {
            std::string spec = argv[++i];
            size_t colon = spec.find(':');
            if (colon != std::string::npos && spec.rfind('.', colon) != std::string::npos)
            {
                int line = std::stoi(spec.substr(colon + 1));
                dbg.addSrcBreak(spec.substr(0, colon), line);
            }
            else
            {
                auto sym = dbg.internLabel(spec);
                dbg.addBreak(sym);
            }
        }
        else if (arg == "--debug-cmds" && i + 1 < argc)
        {
            script = std::make_unique<vm::DebugScript>(argv[++i]);
        }
        else if (arg == "--step")
        {
            stepFlag = true;
        }
        else if (arg == "--continue")
        {
            continueFlag = true;
        }
        else if (arg == "--bounds-checks")
        {
            // Flag accepted for parity with front-end run mode.
        }
        else if (arg == "--watch" && i + 1 < argc)
        {
            dbg.addWatch(argv[++i]);
        }
        else if (arg == "--count")
        {
            countFlag = true;
        }
        else if (arg == "--time")
        {
            timeFlag = true;
        }
        else
        {
            usage();
            return 1;
        }
    }
    if (continueFlag)
    {
        dbg = vm::DebugCtrl();
        script.reset();
        stepFlag = false;
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
    dbg.setSourceManager(traceCfg.sm);
    if (!stdinPath.empty())
    {
        if (!freopen(stdinPath.c_str(), "r", stdin))
        {
            std::cerr << "unable to open stdin file\n";
            return 1;
        }
    }
    if (stepFlag)
    {
        auto it = std::find_if(m.functions.begin(),
                               m.functions.end(),
                               [](const core::Function &f) { return f.name == "main"; });
        if (it != m.functions.end() && !it->blocks.empty())
        {
            auto sym = dbg.internLabel(it->blocks.front().label);
            dbg.addBreak(sym);
        }
        if (!script)
        {
            script = std::make_unique<vm::DebugScript>();
        }
        script->addStep(1);
    }
    vm::VM vm(m, traceCfg, maxSteps, std::move(dbg), script.get());
    std::chrono::steady_clock::time_point start;
    if (timeFlag)
        start = std::chrono::steady_clock::now();
    int rc = static_cast<int>(vm.run());
    std::chrono::steady_clock::time_point end;
    if (timeFlag)
        end = std::chrono::steady_clock::now();
    if (countFlag || timeFlag)
    {
        std::cerr << "[SUMMARY]";
        if (countFlag)
            std::cerr << " instr=" << vm.getInstrCount();
        if (timeFlag)
        {
            double ms = std::chrono::duration<double, std::milli>(end - start).count();
            std::cerr << " time_ms=" << ms;
        }
        std::cerr << "\n";
    }
    return rc;
}
