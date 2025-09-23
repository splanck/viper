// File: src/tools/ilc/cmd_run_il.cpp
// Purpose: Implements execution of IL programs.
// License: MIT (see LICENSE).
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded modules.
// Links: docs/codemap.md

#include "vm/Debug.hpp"
#include "vm/DebugScript.hpp"
#include "vm/Trace.hpp"
#include "break_spec.hpp"
#include "cli.hpp"
#include "il/api/expected_api.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "il/verify/Verifier.hpp"
#include "support/source_manager.hpp"
#include "vm/VM.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <exception>
#include <string>
#include <utility>

using namespace il;

namespace
{

bool tryParseLineNumber(const std::string &token, int &line)
{
    if (token.empty())
    {
        return false;
    }
    for (char ch : token)
    {
        if (!std::isdigit(static_cast<unsigned char>(ch)))
        {
            return false;
        }
    }
    try
    {
        line = std::stoi(token);
    }
    catch (const std::exception &)
    {
        return false;
    }
    return line > 0;
}

void reportInvalidLineNumber(const std::string &lineToken,
                             const std::string &spec,
                             const char *flag)
{
    std::cerr << "invalid line number '" << lineToken << "' for " << flag;
    if (!spec.empty())
    {
        std::cerr << " argument \"" << spec << "\"";
    }
    std::cerr << "\n";
    usage();
}

} // namespace

/**
 * @brief Run an IL program from file.
 *
 * Execution proceeds through the following phases:
 * 1. Process debugging-related CLI flags alongside shared `ilc` options.
 * 2. Read the requested IL file into a module via the expected API.
 * 3. Verify the parsed module and surface diagnostics on failure.
 * 4. Configure the VM with stdin redirection, trace controls, and debugger setup.
 * 5. Execute the program and report optional instruction counts and timing metrics.
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
    ilc::SharedCliOptions sharedOpts;
    vm::DebugCtrl dbg;
    std::unique_ptr<vm::DebugScript> script;
    il::support::SourceManager sm;
    bool stepFlag = false;
    bool continueFlag = false;
    bool countFlag = false;
    bool timeFlag = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--break" && i + 1 < argc)
        {
            std::string spec = argv[++i];
            if (ilc::isSrcBreakSpec(spec))
            {
                auto pos = spec.rfind(':');
                std::string file = spec.substr(0, pos);
                const std::string lineToken = spec.substr(pos + 1);
                int line = 0;
                if (!tryParseLineNumber(lineToken, line))
                {
                    reportInvalidLineNumber(lineToken, spec, "--break");
                    return 1;
                }
                dbg.addBreakSrcLine(file, line);
            }
            else
            {
                auto sym = dbg.internLabel(spec.c_str());
                dbg.addBreak(sym);
            }
        }
        else if (arg == "--break-src" && i + 1 < argc)
        {
            std::string spec = argv[++i];
            auto pos = spec.rfind(':');
            if (pos != std::string::npos)
            {
                std::string file = spec.substr(0, pos);
                const std::string lineToken = spec.substr(pos + 1);
                int line = 0;
                if (!tryParseLineNumber(lineToken, line))
                {
                    reportInvalidLineNumber(lineToken, spec, "--break-src");
                    return 1;
                }
                dbg.addBreakSrcLine(file, line);
            }
            else
            {
                reportInvalidLineNumber("", spec, "--break-src");
                return 1;
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
            switch (ilc::parseSharedOption(i, argc, argv, sharedOpts))
            {
            case ilc::SharedOptionParseResult::Parsed:
                continue;
            case ilc::SharedOptionParseResult::Error:
                usage();
                return 1;
            case ilc::SharedOptionParseResult::NotMatched:
                usage();
                return 1;
            }
        }
    }
    if (continueFlag)
    {
        dbg = vm::DebugCtrl();
        script.reset();
        stepFlag = false;
    }
    sm.addFile(ilFile);
    vm::TraceConfig traceCfg = sharedOpts.trace;
    traceCfg.sm = &sm;
    dbg.setSourceManager(&sm);
    std::ifstream ifs(ilFile);
    if (!ifs)
    {
        std::cerr << "unable to open " << ilFile << "\n";
        return 1;
    }
    core::Module m;
    auto pe = il::api::v2::parse_text_expected(ifs, m);
    if (!pe)
    {
        il::support::printDiag(pe.error(), std::cerr);
        return 1;
    }
    auto ve = il::verify::Verifier::verify(m);
    if (!ve)
    {
        il::support::printDiag(ve.error(), std::cerr);
        return 1;
    }
    if (!sharedOpts.stdinPath.empty())
    {
        if (!freopen(sharedOpts.stdinPath.c_str(), "r", stdin))
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
    vm::VM vm(m, traceCfg, sharedOpts.maxSteps, std::move(dbg), script.get());
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
