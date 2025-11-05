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

/// @file
/// @brief Parses global command-line options shared by ilc subcommands.
/// @details Keeping this logic out of the subcommand implementations minimises
///          duplication and ensures future options behave uniformly across the
///          driver.

#include "cli.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string_view>
#include <system_error>

namespace ilc
{

/// @brief Parse an ilc global option and update the shared options structure.
///
/// @details Recognised options include tracing (`--trace[=mode]`), stdin
///          redirection, instruction limits, bounds checks, and trap dumping.
///          When the option consumes an additional argument the helper advances
///          @p index so the caller continues scanning from the next flag.
///          Failures—such as a missing argument or malformed numeric value—
///          return @ref SharedOptionParseResult::Error so the caller can surface
///          usage information.  Options that do not match are reported as
///          @ref SharedOptionParseResult::NotMatched, allowing subcommands to
///          parse their own flags.
///
/// @param index Current position in the argv array; advanced when extra tokens
///        are consumed (for example by `--stdin-from` or `--max-steps`).
/// @param argc Total argument count for the subcommand.
/// @param argv Argument vector supplied to the driver.
/// @param opts Structure receiving parsed option values.
/// @return Result indicating whether the option was handled, not matched, or
///         produced a parsing error.
namespace
{

std::string toLowerCopy(std::string_view text)
{
    std::string lowered(text.begin(), text.end());
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

} // namespace

std::optional<SharedCliOptions::EngineKind> parseEngineName(std::string_view name)
{
    const std::string lowered = toLowerCopy(name);
    if (lowered == "auto" || lowered.empty())
    {
        return SharedCliOptions::EngineKind::Auto;
    }
    if (lowered == "vm-switch" || lowered == "switch" || lowered == "vm")
    {
        return SharedCliOptions::EngineKind::VmSwitch;
    }
    if (lowered == "vm-table" || lowered == "table" || lowered == "fn-table")
    {
        return SharedCliOptions::EngineKind::VmTable;
    }
    if (lowered == "vm-threaded" || lowered == "threaded")
    {
        return SharedCliOptions::EngineKind::VmThreaded;
    }
    if (lowered == "native" || lowered == "codegen" || lowered == "codegen-x64")
    {
        return SharedCliOptions::EngineKind::Native;
    }
    return std::nullopt;
}

SharedOptionParseResult parseSharedOption(int &index, int argc, char **argv, SharedCliOptions &opts)
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
    if (arg == "--engine")
    {
        if (index + 1 >= argc)
        {
            return SharedOptionParseResult::Error;
        }
        if (auto parsed = parseEngineName(argv[++index]))
        {
            opts.engine = *parsed;
            opts.engineExplicit = true;
            return SharedOptionParseResult::Parsed;
        }
        return SharedOptionParseResult::Error;
    }
    if (arg.rfind("--engine=", 0) == 0)
    {
        const std::string_view value(arg.c_str() + std::string("--engine=").size());
        if (auto parsed = parseEngineName(value))
        {
            opts.engine = *parsed;
            opts.engineExplicit = true;
            return SharedOptionParseResult::Parsed;
        }
        return SharedOptionParseResult::Error;
    }
    return SharedOptionParseResult::NotMatched;
}

} // namespace ilc
