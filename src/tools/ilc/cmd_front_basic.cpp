// File: src/tools/ilc/cmd_front_basic.cpp
// License: MIT License (see LICENSE).
// Purpose: BASIC front-end driver for ilc.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded modules.
// Links: docs/class-catalog.md

#include "vm/Trace.hpp"
#include "cli.hpp"
#include "frontends/basic/ConstFolder.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "il/api/expected_api.hpp"
#include "il/io/Serializer.hpp"
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
 * @brief Compile a BASIC source file to IL.
 *
 * @details The compilation pipeline reads the source from disk, parses it into
 * an AST, folds constants, performs semantic analysis with diagnostics, and
 * finally lowers the validated program into IL.
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
    bool hadErrors = false;
    core::Module m = compileBasicToIL(file, sharedOpts.boundsChecks, hadErrors, sm);
    if (hadErrors)
        return 1;
    if (emitIl)
    {
        io::Serializer::write(m, std::cout);
        return 0;
    }
    auto ve = il::api::v2::verify_module_expected(m);
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
