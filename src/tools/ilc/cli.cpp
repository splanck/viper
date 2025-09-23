// File: src/tools/ilc/cli.cpp
// Purpose: Implements shared CLI option parsing for ilc subcommands.
// Key invariants: None.
// Ownership/Lifetime: N/A.
// License: MIT (see LICENSE).
// Links: docs/codemap.md

#include "cli.hpp"

namespace ilc
{

SharedOptionParseResult parseSharedOption(int &index,
                                         int argc,
                                         char **argv,
                                         SharedCliOptions &opts)
{
    const std::string arg = argv[index];
    if (arg == "--trace" || arg == "--trace=il")
    {
        opts.trace.mode = il::vm::TraceConfig::IL;
        return SharedOptionParseResult::Parsed;
    }
    if (arg == "--trace=src")
    {
        opts.trace.mode = il::vm::TraceConfig::SRC;
        return SharedOptionParseResult::Parsed;
    }
    if (arg == "--stdin-from")
    {
        if (index + 1 >= argc)
        {
            return SharedOptionParseResult::Error;
        }
        opts.stdinPath = argv[++index];
        return SharedOptionParseResult::Parsed;
    }
    if (arg == "--max-steps")
    {
        if (index + 1 >= argc)
        {
            return SharedOptionParseResult::Error;
        }
        opts.maxSteps = std::stoull(argv[++index]);
        return SharedOptionParseResult::Parsed;
    }
    if (arg == "--bounds-checks")
    {
        opts.boundsChecks = true;
        return SharedOptionParseResult::Parsed;
    }
    return SharedOptionParseResult::NotMatched;
}

} // namespace ilc
