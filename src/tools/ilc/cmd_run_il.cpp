//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the `ilc run` subcommand that executes textual IL modules through
// the in-process virtual machine.  The driver coordinates command-line parsing,
// debugger configuration, module loading, verification, and VM execution while
// reporting optional profiling information.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Entry point for the `ilc run` subcommand.
/// @details Provides CLI parsing helpers, debugger configuration utilities, and
///          the glue that loads IL from disk before launching the VM.  The
///          helpers document how tracing, breakpoints, and summary reporting are
///          wired into the driver so new flags can be added consistently.

#include "break_spec.hpp"
#include "cli.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"
#include "tools/common/module_loader.hpp"
#include "vm/Debug.hpp"
#include "vm/DebugScript.hpp"
#include "vm/Trace.hpp"
#include "vm/VM.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace il;

namespace
{

struct RunILConfig
{
    struct SourceBreak
    {
        std::string file;
        uint32_t line = 0;
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
    bool boundsChecksRequested = false;
    vm::DebugCtrl debugCtrl;
    std::unique_ptr<vm::DebugScript> debugScript;
};

/// @brief Trim leading and trailing ASCII whitespace from a string.
/// @details Iterates over the input to find the first and last characters that
///          are not classified as whitespace by @c std::isspace before
///          returning the inclusive substring.  An all-whitespace input yields
///          an empty string, making it convenient for downstream validation
///          logic.
/// @param text Candidate string containing surrounding padding.
/// @return Copy of @p text with outer whitespace removed.
std::string trimWhitespace(std::string text)
{
    auto begin = std::find_if_not(
        text.begin(), text.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    auto end = std::find_if_not(text.rbegin(),
                                text.rend(),
                                [](unsigned char ch) { return std::isspace(ch) != 0; })
                   .base();
    if (begin >= end)
    {
        return std::string();
    }
    return std::string(begin, end);
}

/// @brief Parse a decimal breakpoint line number from a CLI token.
/// @details Validates that @p token contains only digits before accumulating the
///          numeric value.  Successful conversions must be strictly positive and
///          no larger than @c std::numeric_limits<uint32_t>::max().  The parsed
///          value is stored in @p line and the helper returns true.  Failures
///          leave @p line untouched and return false so callers can surface
///          consistent diagnostics.
/// @param token Candidate substring containing the numeric portion of a
///        breakpoint spec.
/// @param line Output slot populated with the parsed value on success.
/// @return True when @p token encodes a positive decimal integer.
bool tryParseLineNumber(const std::string &token, uint32_t &line)
{
    if (token.empty())
    {
        return false;
    }
    uint64_t value = 0;
    for (char ch : token)
    {
        if (!std::isdigit(static_cast<unsigned char>(ch)))
        {
            return false;
        }
        value = value * 10 + static_cast<unsigned>(ch - '0');
        if (value > std::numeric_limits<uint32_t>::max())
        {
            return false;
        }
    }
    if (value == 0)
    {
        return false;
    }
    line = static_cast<uint32_t>(value);
    return true;
}

/// @brief Report a malformed line-number argument and display usage text.
/// @details Prints the offending token alongside the flag that referenced it
///          and then invokes @ref usage() to show help before returning to the
///          caller.  Keeping this logic centralised guarantees identical wording
///          for @c --break and @c --break-src failures.
/// @param lineToken Token that failed validation (may be empty when missing).
/// @param spec Full argument passed to the flag.
/// @param flag Flag name responsible for the argument, such as "--break".
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

/// @brief Decode all `ilc run`-specific command-line arguments.
/// @details Extracts the input file, debugger options, and execution summary
///          toggles while delegating shared options to
///          @ref ilc::parseSharedOption.  Missing operands or unknown flags
///          trigger usage output and a false return value so @ref cmdRunIL can
///          abort gracefully.
/// @param argc Number of arguments supplied to the subcommand.
/// @param argv Argument array (first element is the IL file path).
/// @param config Configuration structure populated with parsed state.
/// @return True on success; false after emitting usage information.
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
                std::string file = trimWhitespace(spec.substr(0, pos));
                const std::string lineToken = trimWhitespace(spec.substr(pos + 1));
                uint32_t line = 0;
                if (!tryParseLineNumber(lineToken, line))
                {
                    reportInvalidLineNumber(lineToken, spec, "--break");
                    return false;
                }
                config.breakSrcLines.push_back({std::move(file), line});
            }
            else
            {
                std::string trimmedSpec = trimWhitespace(spec);
                bool parsedAsSrcBreak = false;
                auto pos = trimmedSpec.rfind(':');
                if (pos != std::string::npos)
                {
                    std::string file = trimWhitespace(trimmedSpec.substr(0, pos));
                    const std::string lineToken = trimWhitespace(trimmedSpec.substr(pos + 1));
                    if (!lineToken.empty())
                    {
                        uint32_t line = 0;
                        if (!tryParseLineNumber(lineToken, line))
                        {
                            // Fall through to label handling when the suffix is not a
                            // valid line number.
                        }
                        else if (file.empty())
                        {
                            reportInvalidLineNumber(lineToken, spec, "--break");
                            return false;
                        }
                        else
                        {
                            config.breakSrcLines.push_back({std::move(file), line});
                            parsedAsSrcBreak = true;
                        }
                    }
                }

                if (parsedAsSrcBreak)
                {
                    continue;
                }

                std::string label = std::move(trimmedSpec);
                while (!label.empty() && label.back() == ':')
                {
                    label.pop_back();
                }
                config.breakLabels.push_back(std::move(label));
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
                std::string file = trimWhitespace(spec.substr(0, pos));
                const std::string lineToken = trimWhitespace(spec.substr(pos + 1));
                if (file.empty())
                {
                    reportInvalidLineNumber(lineToken, spec, "--break-src");
                    return false;
                }
                uint32_t line = 0;
                if (!tryParseLineNumber(lineToken, line))
                {
                    reportInvalidLineNumber(lineToken, spec, "--break-src");
                    return false;
                }
                config.breakSrcLines.push_back({std::move(file), line});
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

    config.boundsChecksRequested = config.sharedOpts.boundsChecks;

    return true;
}

/// @brief Configure debugger state based on parsed CLI options.
/// @details Resets the debugger when @c --continue is present; otherwise interns
///          label and source breakpoints, registers watch expressions, and
///          materialises a @ref vm::DebugScript for scripted or step-driven
///          execution.  Stepping without an existing script creates a transient
///          script that requests a single step.
/// @param config Parsed `run` configuration describing debugger behaviour.
/// @param dbg Debug controller to update.
/// @param script Optional debug script owned by the caller; allocated or
///        cleared depending on CLI flags.
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

/// @brief Load, verify, and execute the requested IL module.
/// @details Sets up diagnostics infrastructure, applies tracing configuration,
///          loads the module from disk, and runs verification before launching
///          the VM.  When execution finishes optional instruction counts and
///          timing summaries are printed, and any trap messages are surfaced on
///          stderr.  Returns a non-zero status when any phase fails.
/// @param config Fully populated configuration for the run.
/// @return Process-style exit status; zero indicates success.
int executeRunIL(const RunILConfig &config, il::support::SourceManager &sm)
{
    if (config.boundsChecksRequested)
    {
        std::cerr << "error: --bounds-checks is not supported when running existing IL modules;";
        std::cerr << " recompile the source with bounds checks enabled and rerun.\n";
        return 1;
    }

    const uint32_t fileId = sm.addFile(config.ilFile);
    if (fileId == 0)
    {
        return 1;
    }

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
    const int64_t runResult = vm.run();
    int rc = 0;
    const auto intMin = std::numeric_limits<int>::min();
    const auto intMax = std::numeric_limits<int>::max();
    if (runResult < intMin || runResult > intMax)
    {
        std::cerr << "ilc run: program return value " << runResult
                  << " outside host int range [" << intMin << ", " << intMax << "]\n";
        rc = 1;
    }
    else
    {
        rc = static_cast<int>(runResult);
    }
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

/// @brief Execute the `run` subcommand with a caller-provided source manager.
/// @details Parses arguments, configures debugger state, and then dispatches to
///          @ref executeRunIL using the supplied @p sm.  Tests use this helper
///          to preconfigure overflow conditions deterministically.
/// @param argc Number of subcommand arguments (excluding the subcommand).
/// @param argv Argument vector beginning with the IL file path.
/// @param sm Source manager instance prepared by the caller.
/// @return Zero on success; non-zero when parsing or execution fails.
int cmdRunILWithSourceManager(int argc, char **argv, il::support::SourceManager &sm)
{
    RunILConfig config;
    if (!parseRunILArgs(argc, argv, config))
    {
        return 1;
    }

    configureDebugger(config, config.debugCtrl, config.debugScript);
    return executeRunIL(config, sm);
}

/// @brief Execute the `run` subcommand end-to-end.
/// @details Parses arguments, configures debugger state, and then dispatches to
///          @ref executeRunIL.  Parsing failures are surfaced via a non-zero
///          return code so the outer driver can present diagnostics.
/// @param argc Number of subcommand arguments (excluding the subcommand).
/// @param argv Argument vector beginning with the IL file path.
/// @return Zero on success; non-zero when parsing or execution fails.
int cmdRunIL(int argc, char **argv)
{
    il::support::SourceManager sm;
    return cmdRunILWithSourceManager(argc, argv, sm);
}
