//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/ilc/cmd_codegen_x64.cpp
// Purpose: Provide a thin CLI adapter around the x86-64 code-generation pipeline.
// Key invariants: Command-line parsing emits deterministic diagnostics and defers heavy lifting
//                 to CodegenPipeline. Ownership/Lifetime: Arguments are borrowed for the duration
//                 of parsing; compilation artefacts are produced by the pipeline implementation.
// Links: src/codegen/x86_64/CodegenPipeline.hpp
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the `ilc codegen x64` command-line entry point.
/// @details Parses argv-style arguments into pipeline options before delegating to the
///          reusable pipeline implementation.

#include "cmd_codegen_x64.hpp"

#include "codegen/x86_64/CodegenPipeline.hpp"

#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace viper::tools::ilc
{
namespace
{

constexpr std::string_view kUsage =
    "usage: ilc codegen x64 <file.il> [-S <file.s>] [-o <a.out>] [-run-native]\n";

/// @brief Lightweight non-owning view over argv-style argument arrays.
/// @details Encapsulates the argument count and pointer pair supplied by the C
///          runtime so helpers can inspect and slice the list without copying.
struct ArgvView
{
    int argc;
    char **argv;

    /// @brief Determine whether the view contains no arguments.
    /// @return True when `argc` is non-positive or the pointer is null.
    [[nodiscard]] bool empty() const
    {
        return argc <= 0 || argv == nullptr;
    }

    /// @brief Access the first argument in the sequence.
    /// @details Returns an empty view when the sequence is empty.
    /// @return String view referencing the first argument.
    [[nodiscard]] std::string_view front() const
    {
        return empty() ? std::string_view{} : std::string_view(argv[0]);
    }

    /// @brief Read the argument at @p index, returning an empty view on overflow.
    /// @param index Offset into the argv array.
    /// @return View over the requested argument or empty view when invalid.
    [[nodiscard]] std::string_view at(int index) const
    {
        if (index < 0 || index >= argc || argv == nullptr)
        {
            return std::string_view{};
        }
        return std::string_view(argv[index]);
    }

    /// @brief Produce a suffix view that skips the first @p count entries.
    /// @param count Number of arguments to drop.
    /// @return New view representing the remaining arguments.
    [[nodiscard]] ArgvView drop_front(int count = 1) const
    {
        if (count >= argc)
        {
            return ArgvView{0, nullptr};
        }
        return ArgvView{argc - count, argv + count};
    }
};

/// @brief Result bundle produced by @ref parseCompileArgs.
/// @details Contains the successfully parsed options or a diagnostic string when
///          parsing failed.
struct ParseOutcome
{
    std::optional<viper::codegen::x64::CodegenPipeline::Options> opts{};
    std::string diagnostics{};
};

/// @brief Decode `ilc codegen x64 compile` arguments into pipeline options.
/// @details Validates positional arguments, handles recognised flags, and emits
///          user-friendly diagnostics on failure.
/// @param args View of the arguments following `codegen x64`.
/// @return Parsed options or diagnostics describing the failure.
ParseOutcome parseCompileArgs(const ArgvView &args)
{
    ParseOutcome outcome{};
    if (args.empty())
    {
        outcome.diagnostics = std::string{kUsage};
        return outcome;
    }

    viper::codegen::x64::CodegenPipeline::Options opts{};
    opts.input_il_path = std::string(args.front());
    opts.output_obj_path.clear();
    opts.output_asm_path.clear();

    std::ostringstream diag;
    for (int index = 1; index < args.argc; ++index)
    {
        const std::string_view arg = args.at(index);
        if (arg == "-S")
        {
            if (index + 1 >= args.argc)
            {
                diag << "error: -S requires an output path\n" << kUsage;
                outcome.diagnostics = diag.str();
                return outcome;
            }
            opts.emit_asm = true;
            opts.output_asm_path = std::string(args.at(++index));
            continue;
        }
        if (arg == "-o")
        {
            if (index + 1 >= args.argc)
            {
                diag << "error: -o requires an output path\n" << kUsage;
                outcome.diagnostics = diag.str();
                return outcome;
            }
            opts.output_obj_path = std::string(args.at(++index));
            continue;
        }
        if (arg == "-run-native")
        {
            opts.run_native = true;
            continue;
        }

        diag << "error: unknown flag '" << arg << "'\n" << kUsage;
        outcome.diagnostics = diag.str();
        return outcome;
    }

    outcome.opts = std::move(opts);
    return outcome;
}

/// @brief Execute the `compile` handler for the x64 codegen driver.
/// @details Parses arguments via @ref parseCompileArgs and, when successful,
///          runs the code generation pipeline before forwarding captured
///          stdout/stderr to the caller.
/// @param args View over the user-provided arguments.
/// @return Zero on success; otherwise non-zero to signal failure.
int handleCompile(const ArgvView &args)
{
    const ParseOutcome parsed = parseCompileArgs(args);
    if (!parsed.opts.has_value())
    {
        if (!parsed.diagnostics.empty())
        {
            std::cerr << parsed.diagnostics;
        }
        return 1;
    }

    viper::codegen::x64::CodegenPipeline pipeline(*parsed.opts);
    const PipelineResult result = pipeline.run();

    if (!result.stdout_text.empty())
    {
        std::cout << result.stdout_text;
    }
    if (!result.stderr_text.empty())
    {
        std::cerr << result.stderr_text;
    }
    return result.exit_code;
}

using Handler = int (*)(const ArgvView &);

const std::unordered_map<std::string, Handler> kHandlers = {
    {"compile", &handleCompile},
};

} // namespace

/// @brief Dispatch entry point for the `codegen x64` driver.
/// @details Routes to known subcommands (currently only `compile`).  Unknown
///          tokens fall back to `compile` so one-off invocations like
///          `ilc codegen x64 foo.il` still succeed.
/// @param argc Argument count supplied by the CLI harness.
/// @param argv Argument vector supplied by the CLI harness.
/// @return Exit code reported by the chosen handler.
int cmd_codegen_x64(int argc, char **argv)
{
    const ArgvView args{argc, argv};
    if (args.empty())
    {
        std::cerr << kUsage;
        return 1;
    }

    const std::string_view token = args.front();
    if (const auto it = kHandlers.find(std::string(token)); it != kHandlers.end())
    {
        return it->second(args.drop_front());
    }

    return handleCompile(args);
}

/// @brief Register x64 codegen commands with the shared CLI object.
/// @details Present for symmetry with other command registration helpers.  The
///          current driver wires subcommands manually so the function is a
///          no-op.
void register_codegen_x64_commands(CLI &cli)
{
    (void)cli;
}

} // namespace viper::tools::ilc
