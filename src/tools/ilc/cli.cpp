//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements shared command-line parsing for the ilc driver.  The helpers here
// decode the global options that apply to multiple subcommands so individual
// entry points can focus on their feature-specific flags.
//
//===----------------------------------------------------------------------===//

#include "cli.hpp"

#include <charconv>
#include <string_view>
#include <system_error>

namespace ilc
{

/// @brief Parse an ilc global option and update the shared options structure.
///
/// The parser recognises tracing flags, input redirection, execution limits,
/// and diagnostic controls.  On success the current @p index is advanced past
/// any consumed arguments so the caller can continue scanning remaining flags.
///
/// @param index Current position in the argv array; advanced when extra tokens
///        are consumed (for example by `--stdin-from` or `--max-steps`).
/// @param argc Total argument count for the subcommand.
/// @param argv Argument vector supplied to the driver.
/// @param opts Structure receiving parsed option values.
/// @return Result indicating whether the option was handled, not matched, or
///         produced a parsing error.
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
