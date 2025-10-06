// File: src/tools/ilc/cli.cpp
// Purpose: Implements shared CLI option parsing for ilc subcommands.
// Key invariants: None.
// Ownership/Lifetime: N/A.
// License: MIT (see LICENSE).
// Links: docs/codemap.md

#include "cli.hpp"

#include <charconv>
#include <string_view>
#include <system_error>

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
        std::string_view value(argv[index + 1]);
        std::uint64_t parsed = 0;
        const char *const begin = value.data();
        const char *const end = begin + value.size();
        const auto fc = std::from_chars(begin, end, parsed);
        if (fc.ec != std::errc() || fc.ptr != end)
        {
            return SharedOptionParseResult::Error;
        }
        ++index;
        opts.maxSteps = parsed;
        return SharedOptionParseResult::Parsed;
    }
    if (arg == "--bounds-checks")
    {
        opts.boundsChecks = true;
        return SharedOptionParseResult::Parsed;
    }
    if (arg == "--dump-trap")
    {
        opts.dumpTrap = true;
        return SharedOptionParseResult::Parsed;
    }
    return SharedOptionParseResult::NotMatched;
}

} // namespace ilc
