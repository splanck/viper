// File: src/tools/ilc/cmd_front_basic.cpp
// License: MIT License (see LICENSE).
// Purpose: BASIC front-end driver for ilc.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded modules.
// Links: docs/codemap.md

#include "vm/Trace.hpp"
#include "cli.hpp"
#include "frontends/basic/BasicCompiler.hpp"
#include "il/io/Serializer.hpp"
#include "il/api/expected_api.hpp"
#include "il/verify/Verifier.hpp"
#include "support/source_manager.hpp"
#include "vm/VM.hpp"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace il;
using namespace il::frontends::basic;
using namespace il::support;

/**
 * @brief Handle BASIC front-end subcommands.
 *
 * @details Parses ilc shared options alongside BASIC-specific flags, then
 * compiles the requested input, optionally emits IL, verifies the module, and
 * runs it in the VM when requested.
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
    ilc::SharedCliOptions sharedOpts;
    SourceManager sm;
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
    std::string source = ss.str();

    BasicCompilerOptions compilerOpts{};
    compilerOpts.boundsChecks = sharedOpts.boundsChecks;
    BasicCompilerInput compilerInput{source, file};
    auto result = compileBasic(compilerInput, compilerOpts, sm);
    if (!result.succeeded())
    {
        if (result.emitter)
        {
            result.emitter->printAll(std::cerr);
        }
        return 1;
    }
    core::Module m = std::move(result.module);
    if (emitIl)
    {
        io::Serializer::write(m, std::cout);
        return 0;
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
    vm::TraceConfig traceCfg = sharedOpts.trace;
    traceCfg.sm = &sm;
    vm::VM vm(m, traceCfg, sharedOpts.maxSteps);
    return static_cast<int>(vm.run());
}
