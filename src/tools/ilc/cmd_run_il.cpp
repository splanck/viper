// File: src/tools/ilc/cmd_run_il.cpp
// Purpose: Implements execution of IL programs.
// License: MIT (see LICENSE).
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded modules.
// Links: docs/codemap.md

#include "break_spec.hpp"
#include "cli.hpp"
#include "tools/common/module_loader.hpp"
#include "vm/Debug.hpp"
#include "vm/DebugScript.hpp"
#include "vm/Trace.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"
#include "vm/VM.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <exception>
#include <vector>
#include <string>
#include <utility>

using namespace il;

namespace
{

struct RunILConfig
{
    struct SourceBreak
    {
        std::string file;
        int line = 0;
    };

    std::string ilFile;
    ilc::SharedCliOptions sharedOpts;
    std::vector<std::string> breakLabels;
    std::vector<SourceBreak> breakSrcLines;
    std::vector<std::string> watchSymbols;
    std::string debugScriptPath;
    bool stepFlag = false;
    bool continueFlag = false;
    bool countFlag = false;
    bool timeFlag = false;
    vm::DebugCtrl debugCtrl;
    std::unique_ptr<vm::DebugScript> debugScript;
};

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

bool parseRunILArgs(int argc, char **argv, RunILConfig &config)
{
    if (argc < 1)
    {
        usage();
        return false;
    }

    config.ilFile = argv[0];

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--break")
        {
            if (i + 1 >= argc)
            {
                usage();
                return false;
            }
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
                    return false;
                }
                config.breakSrcLines.push_back({ std::move(file), line });
            }
            else
            {
                config.breakLabels.push_back(std::move(spec));
            }
        }
        else if (arg == "--break-src")
        {
            if (i + 1 >= argc)
            {
                usage();
                return false;
            }
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
                    return false;
                }
                config.breakSrcLines.push_back({ std::move(file), line });
            }
            else
            {
                reportInvalidLineNumber("", spec, "--break-src");
                return false;
            }
        }
        else if (arg == "--debug-cmds")
        {
            if (i + 1 >= argc)
            {
                usage();
                return false;
            }
            config.debugScriptPath = argv[++i];
        }
        else if (arg == "--step")
        {
            config.stepFlag = true;
        }
        else if (arg == "--continue")
        {
            config.continueFlag = true;
        }
        else if (arg == "--watch")
        {
            if (i + 1 >= argc)
            {
                usage();
                return false;
            }
            config.watchSymbols.emplace_back(argv[++i]);
        }
        else if (arg == "--count")
        {
            config.countFlag = true;
        }
        else if (arg == "--time")
        {
            config.timeFlag = true;
        }
        else
        {
            switch (ilc::parseSharedOption(i, argc, argv, config.sharedOpts))
            {
            case ilc::SharedOptionParseResult::Parsed:
                continue;
            case ilc::SharedOptionParseResult::Error:
                usage();
                return false;
            case ilc::SharedOptionParseResult::NotMatched:
                usage();
                return false;
            }
        }
    }

    if (config.continueFlag)
    {
        config.stepFlag = false;
    }

    return true;
}

void configureDebugger(const RunILConfig &config,
                       vm::DebugCtrl &dbg,
                       std::unique_ptr<vm::DebugScript> &script)
{
    if (config.continueFlag)
    {
        dbg = vm::DebugCtrl();
        script.reset();
        return;
    }

    for (const auto &label : config.breakLabels)
    {
        auto sym = dbg.internLabel(label.c_str());
        dbg.addBreak(sym);
    }
    for (const auto &src : config.breakSrcLines)
    {
        dbg.addBreakSrcLine(src.file, src.line);
    }
    for (const auto &watch : config.watchSymbols)
    {
        dbg.addWatch(watch);
    }

    if (!config.debugScriptPath.empty())
    {
        script = std::make_unique<vm::DebugScript>(config.debugScriptPath);
    }
    if (config.stepFlag)
    {
        if (!script)
        {
            script = std::make_unique<vm::DebugScript>();
        }
        script->addStep(1);
    }
}

int executeRunIL(const RunILConfig &config)
{
    il::support::SourceManager sm;
    sm.addFile(config.ilFile);

    vm::TraceConfig traceCfg = config.sharedOpts.trace;
    traceCfg.sm = &sm;

    vm::DebugCtrl dbg = config.debugCtrl;
    dbg.setSourceManager(&sm);

    core::Module m;
    auto load = il::tools::common::loadModuleFromFile(config.ilFile, m, std::cerr);
    if (!load.succeeded())
    {
        return 1;
    }

    if (!il::tools::common::verifyModule(m, std::cerr, &sm))
    {
        return 1;
    }

    if (!config.sharedOpts.stdinPath.empty())
    {
        if (!freopen(config.sharedOpts.stdinPath.c_str(), "r", stdin))
        {
            std::cerr << "unable to open stdin file\n";
            return 1;
        }
    }

    if (config.stepFlag)
    {
        auto it = std::find_if(m.functions.begin(),
                               m.functions.end(),
                               [](const core::Function &f) { return f.name == "main"; });
        if (it != m.functions.end() && !it->blocks.empty())
        {
            auto sym = dbg.internLabel(it->blocks.front().label);
            dbg.addBreak(sym);
        }
    }

    vm::VM vm(m,
              traceCfg,
              config.sharedOpts.maxSteps,
              std::move(dbg),
              config.debugScript ? config.debugScript.get() : nullptr);

    std::chrono::steady_clock::time_point start;
    if (config.timeFlag)
    {
        start = std::chrono::steady_clock::now();
    }
    int rc = static_cast<int>(vm.run());
    const auto trapMessage = vm.lastTrapMessage();
    if (trapMessage)
    {
        if (config.sharedOpts.dumpTrap && !trapMessage->empty())
        {
            std::cerr << *trapMessage;
            if (trapMessage->back() != '\n')
            {
                std::cerr << '\n';
            }
        }
        if (rc == 0)
        {
            rc = 1;
        }
    }
    std::chrono::steady_clock::time_point end;
    if (config.timeFlag)
    {
        end = std::chrono::steady_clock::now();
    }

    if (config.countFlag || config.timeFlag)
    {
        std::cerr << "[SUMMARY]";
        if (config.countFlag)
        {
            std::cerr << " instr=" << vm.getInstrCount();
        }
        if (config.timeFlag)
        {
            double ms = std::chrono::duration<double, std::milli>(end - start).count();
            std::cerr << " time_ms=" << ms;
        }
        std::cerr << "\n";
    }

    return rc;
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
    RunILConfig config;
    if (!parseRunILArgs(argc, argv, config))
    {
        return 1;
    }

    configureDebugger(config, config.debugCtrl, config.debugScript);
    return executeRunIL(config);
}
