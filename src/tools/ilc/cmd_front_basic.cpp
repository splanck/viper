// File: src/tools/ilc/cmd_front_basic.cpp
// Purpose: BASIC front-end driver for ilc.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded modules.
// Links: docs/class-catalog.md

#include "VM/DebugScript.h"
#include "VM/Trace.h"
#include "cli.hpp"
#include "frontends/basic/ConstFolder.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "il/io/Serializer.hpp"
#include "il/verify/Verifier.hpp"
#include "support/source_manager.hpp"
#include "vm/VM.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace il;
using namespace il::frontends::basic;
using namespace il::support;

/**
 * @brief Compile a BASIC source file to IL.
 *
 * @param path Path to the BASIC source.
 * @param boundsChecks Enable bounds checks during lowering.
 * @param hadErrors Set to true if compilation fails.
 * @return Compiled IL module or empty module on error.
 */
static core::Module compileBasicToIL(const std::string &path,
                                     bool boundsChecks,
                                     bool &hadErrors,
                                     SourceManager &sm)
{
    hadErrors = true;
    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "unable to open " << path << "\n";
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string src = ss.str();
    uint32_t fid = sm.addFile(path);
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
        return {};
    }
    Lowerer lower(boundsChecks);
    hadErrors = false;
    return lower.lower(*prog);
}

/**
 * @brief Handle BASIC front-end subcommands.
 *
 * @param argc Number of subcommand arguments (excluding `front basic`).
 * @param argv Argument list.
 * @return Exit status code.
 */
int cmdFrontBasic(int argc, char **argv)
{
    bool emitIl = false;
    bool run = false;
    std::string file;
    std::string stdinPath;
    uint64_t maxSteps = 0;
    bool boundsChecks = false;
    vm::TraceConfig traceCfg{};
    SourceManager sm;
    vm::DebugCtrl dbg;
    std::unique_ptr<vm::DebugScript> script;
    bool stepFlag = false;
    bool continueFlag = false;
    bool countFlag = false;
    bool timeFlag = false;
    for (int i = 0; i < argc; ++i)
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
        else if (arg == "--trace" || arg == "--trace=il")
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
        else if (arg == "--bounds-checks")
        {
            boundsChecks = true;
        }
        else if (arg == "--break" && i + 1 < argc)
        {
            std::string spec = argv[++i];
            size_t c = spec.rfind(':');
            if (c != std::string::npos && spec.substr(0, c).find('.') != std::string::npos)
            {
                int line = std::stoi(spec.substr(c + 1));
                dbg.addBreakSrc(spec.substr(0, c), line);
            }
            else
            {
                auto sym = dbg.internLabel(spec);
                dbg.addBreakLabel(sym);
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
    if ((emitIl == run) || file.empty())
    {
        usage();
        return 1;
    }
    bool hadErrors = false;
    core::Module m = compileBasicToIL(file, boundsChecks, hadErrors, sm);
    if (hadErrors)
        return 1;
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
    if (continueFlag)
    {
        dbg = vm::DebugCtrl();
        script.reset();
        stepFlag = false;
    }
    if (stepFlag)
    {
        auto it = std::find_if(m.functions.begin(),
                               m.functions.end(),
                               [](const core::Function &f) { return f.name == "main"; });
        if (it != m.functions.end() && !it->blocks.empty())
        {
            auto sym = dbg.internLabel(it->blocks.front().label);
            dbg.addBreakLabel(sym);
        }
        if (!script)
            script = std::make_unique<vm::DebugScript>();
        script->addStep(1);
    }
    traceCfg.sm = &sm;
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
