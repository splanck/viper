//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/ilc/cmd_codegen_arm64.cpp
// Purpose: CLI glue for `ilc codegen arm64` supporting -S, -o, and -run-native.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief CLI implementation for the `ilc codegen arm64` subcommand.
/// @details Parses arm64-specific flags, lowers IL to AArch64 MIR, emits
///          assembly, and can optionally assemble, link, and execute native
///          output using the host toolchain.

#include "cmd_codegen_arm64.hpp"

#include "codegen/aarch64/AsmEmitter.hpp"
#include "codegen/aarch64/LowerILToMIR.hpp"
#include "codegen/aarch64/Peephole.hpp"
#include "codegen/aarch64/RegAllocLinear.hpp"
#include "codegen/aarch64/RodataPool.hpp"
#include "codegen/common/LabelUtil.hpp"
#include "codegen/common/LinkerSupport.hpp"
#include "common/RunProcess.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "tools/common/ArgvView.hpp"
#include "il/transform/PassManager.hpp"
#include "tools/common/module_loader.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace viper::tools::ilc
{
namespace
{

using il::core::Opcode;

// No local condForOpcode; mapping lives in LowerILToMIR.

// Small helper that encapsulates the pattern-based lowering used by the arm64 CLI.
// Keeps cmd driver tidy and centralizes opcode/sequence mapping.
// (Pattern-lowering moved to LowerILToMIR)

/// @brief Usage text emitted when argument parsing fails or is incomplete.
/// @details The string mirrors the supported flags in @ref parseArgs so the CLI
///          can emit a single source of truth for help and diagnostics.
constexpr std::string_view kUsage =
    "usage: ilc codegen arm64 <file.il> [-S <file.s>] [-o <a.out>] [-run-native]\n"
    "       [--dump-mir-before-ra] [--dump-mir-after-ra] [--dump-mir-full]\n";

// Use shared ArgvView from tools/common
using viper::tools::ArgvView;

/// @brief Parsed CLI options for the arm64 codegen subcommand.
/// @details Captures output destinations, flags, and diagnostics preferences
///          so the rest of the pipeline can focus on lowering and emission.
struct Options
{
    std::string input_il;                ///< Input IL path provided on the CLI.
    std::optional<std::string> output_s; ///< Explicit assembly output path when -S is used.
    std::optional<std::string> output_o; ///< Optional object/executable output path (-o).
    bool emit_asm = false;               ///< True when -S requests assembly emission.
    bool run_native = false;             ///< True when -run-native requests execution.
    bool dump_mir_before_ra = false;     ///< Emit MIR before register allocation to stderr.
    bool dump_mir_after_ra = false;      ///< Emit MIR after register allocation to stderr.
    int optimize = 0;                    ///< IL optimization level: 0=none, 1=O1, 2=O2.
};

/// @brief Parse argv-style arguments into a structured @ref Options instance.
/// @details Validates required positional arguments and supported flags. The
///          parser emits the shared usage string and a descriptive error when
///          an argument is missing or unrecognized. Supported options include:
///          - `-S <path>` to emit assembly
///          - `-o <path>` to choose object/executable output
///          - `-run-native` to link and execute the result
///          - MIR dumping flags for debugging
/// @param args View of the argument vector (excluding the subcommand name).
/// @return Populated Options on success; std::nullopt on validation failure.
std::optional<Options> parseArgs(const ArgvView &args)
{
    if (args.empty())
    {
        std::cerr << kUsage;
        return std::nullopt;
    }
    Options opts;
    opts.input_il = std::string(args.front());
    for (int i = 1; i < args.argc; ++i)
    {
        const std::string_view tok = args.at(i);
        if (tok == "-S")
        {
            if (i + 1 >= args.argc)
            {
                std::cerr << "error: -S requires an output path\n" << kUsage;
                return std::nullopt;
            }
            opts.emit_asm = true;
            opts.output_s = std::string(args.at(++i));
            continue;
        }
        if (tok == "-o")
        {
            if (i + 1 >= args.argc)
            {
                std::cerr << "error: -o requires an output path\n" << kUsage;
                return std::nullopt;
            }
            opts.output_o = std::string(args.at(++i));
            continue;
        }
        if (tok == "-run-native")
        {
            opts.run_native = true;
            continue;
        }
        if (tok == "--dump-mir-before-ra")
        {
            opts.dump_mir_before_ra = true;
            continue;
        }
        if (tok == "--dump-mir-after-ra")
        {
            opts.dump_mir_after_ra = true;
            continue;
        }
        if (tok == "--dump-mir-full")
        {
            opts.dump_mir_before_ra = true;
            opts.dump_mir_after_ra = true;
            continue;
        }
        if (tok == "-O" || tok == "--optimize")
        {
            if (i + 1 >= args.argc)
            {
                std::cerr << "error: -O requires a level (0, 1, or 2)\n" << kUsage;
                return std::nullopt;
            }
            opts.optimize = std::atoi(std::string(args.at(++i)).c_str());
            continue;
        }
        if (tok.size() == 3 && tok[0] == '-' && tok[1] == 'O' && tok[2] >= '0' && tok[2] <= '2')
        {
            opts.optimize = tok[2] - '0';
            continue;
        }
        std::cerr << "error: unknown flag '" << tok << "'\n" << kUsage;
        return std::nullopt;
    }
    return opts;
}

/// @brief Emit pooled module-level string constants for AArch64 assembly.
/// @details Delegates to the @ref viper::codegen::aarch64::RodataPool, which
///          ensures deterministic labels and assembler-safe string contents.
/// @param os Output stream receiving the `.rodata` fragments.
/// @param pool Pre-populated rodata pool built from the IL module.
static void emitGlobalsAArch64(std::ostream &os, const viper::codegen::aarch64::RodataPool &pool)
{
    pool.emit(os);
}

/// @brief Write text to disk, replacing any existing file.
/// @details Opens @p path in binary truncate mode, streams @p text into the file,
///          and reports IO failures to @p err so the caller can surface them.
/// @param path Destination filesystem path.
/// @param text UTF-8 assembly text to write.
/// @param err Stream for diagnostic output on failure.
/// @return True if the file was written successfully; false on open or write errors.
static bool writeTextFile(const std::string &path, const std::string &text, std::ostream &err)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        err << "error: unable to open '" << path << "' for writing\n";
        return false;
    }
    out << text;
    if (!out)
    {
        err << "error: failed to write file '" << path << "'\n";
        return false;
    }
    return true;
}

/// @brief Link assembly into a native executable, adding runtime archives as needed.
/// @details Delegates to shared linker support for symbol scanning, component
///          resolution, archive discovery, and cmake builds. Constructs the
///          arm64-specific link command and invokes the system linker.
static int linkToExe(const std::string &asmPath,
                     const std::string &exePath,
                     std::ostream &out,
                     std::ostream &err)
{
    using namespace viper::codegen::common;

    LinkContext ctx;
    if (const int rc = prepareLinkContext(asmPath, ctx, out, err); rc != 0)
        return rc;

    // Select the linker front-end and architecture flag based on host OS (MED-11).
    // - macOS: `cc -arch arm64` (Clang driver; explicit arch required for fat-binary hosts)
    // - Windows: `clang --target=aarch64-pc-windows-msvc` (LLVM cross-linker)
    // - Linux/other Unix: `cc` with no -arch (already running on native ARM64)
#if defined(__APPLE__)
    std::vector<std::string> linkCmd = {"cc", "-arch", "arm64", asmPath};
#elif defined(_WIN32)
    std::vector<std::string> linkCmd = {"clang", "--target=aarch64-pc-windows-msvc", asmPath};
#else
    std::vector<std::string> linkCmd = {"cc", asmPath};
#endif
    appendArchives(ctx, linkCmd);
    // UniformTypeIdentifiers is required by vipergui's native file dialog on macOS.
    appendGraphicsLibs(ctx, linkCmd, {"Cocoa", "IOKit", "CoreFoundation", "UniformTypeIdentifiers"});

    // C++ runtime archives (e.g. Threads) need the C++ standard library.
    if (hasComponent(ctx, codegen::RtComponent::Threads))
        linkCmd.push_back("-lc++");

#if defined(__APPLE__)
    linkCmd.push_back("-Wl,-dead_strip");
#elif !defined(_WIN32)
    linkCmd.push_back("-Wl,--gc-sections");
    if (hasComponent(ctx, codegen::RtComponent::Threads))
        linkCmd.push_back("-pthread");
#endif

    linkCmd.push_back("-o");
    linkCmd.push_back(exePath);

    const RunResult rr = run_process(linkCmd);
    if (rr.exit_code == -1)
    {
        err << "error: failed to launch system linker command\n";
        return -1;
    }
    if (!rr.out.empty())
        out << rr.out;
#if defined(_WIN32)
    if (!rr.err.empty())
        err << rr.err;
#endif
    return rr.exit_code == 0 ? 0 : 1;
}

/// @brief Emit assembly and optionally assemble, link, and run native output.
/// @details Loads the IL module from disk, lowers each function to MIR, runs
///          register allocation and peephole optimizations, and emits assembly
///          into a single text buffer. On Darwin targets, symbol fixups are
///          applied so the host toolchain can link against runtime symbols.
///          The function then writes the assembly to disk, and depending on
///          flags, assembles to an object file, links an executable, and
///          optionally executes it. Errors are reported via stderr and a
///          non-zero status code.
/// @param opts Parsed command-line options controlling emission and linkage.
/// @return Zero on success; non-zero on load, codegen, IO, or tool failures.
int emitAndMaybeLink(const Options &opts)
{
    std::ostringstream err;
    il::core::Module mod;
    const auto load = il::tools::common::loadModuleFromFile(opts.input_il, mod, err);
    if (!load.succeeded())
    {
        std::cerr << err.str();
        return 1;
    }

    // Run IL optimizations before lowering to MIR.
    // Codegen-safe pipelines omit LICM and check-opt (known correctness
    // issues).  SCCP and inlining are safe and enabled.
    if (opts.optimize >= 1)
    {
        il::transform::PassManager ilpm;
        if (opts.optimize >= 2)
        {
            ilpm.registerPipeline("codegen-O2",
                                  {"simplify-cfg", "mem2reg",  "simplify-cfg",
                                   "sccp",         "dce",      "simplify-cfg",
                                   "inline",       "simplify-cfg", "dce",
                                   "sccp",         "gvn",      "earlycse", "dse",
                                   "peephole",     "dce",      "late-cleanup"});
            ilpm.runPipeline(mod, "codegen-O2");
        }
        else
        {
            ilpm.registerPipeline("codegen-O1",
                                  {"simplify-cfg", "mem2reg", "simplify-cfg",
                                   "sccp",         "dce",     "simplify-cfg",
                                   "peephole",     "dce"});
            ilpm.runPipeline(mod, "codegen-O1");
        }
    }

    using namespace viper::codegen::aarch64;

    // Host gating for --run-native: only allow on macOS arm64
    if (opts.run_native)
    {
#if !(defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__)))
        std::cerr << "error: --run-native is only supported on macOS arm64 hosts\n";
        return 1;
#endif
    }
#if defined(_WIN32)
    auto &ti = windowsTarget();
#elif defined(__APPLE__)
    auto &ti = darwinTarget();
#else
    auto &ti = linuxTarget();
#endif
    AsmEmitter emitter{ti};
    LowerILToMIR lowerer{ti};

    // Build a pooled view of rodata and emit at file start
    viper::codegen::aarch64::RodataPool pool;
    pool.buildFromModule(mod);

    std::ostringstream asmStream;
    emitGlobalsAArch64(asmStream, pool);
    for (const auto &fn : mod.functions)
    {
        MFunction mir = lowerer.lowerFunction(fn);
        // 1) Sanitize basic block labels and (optionally) uniquify across module
        {
            using viper::codegen::common::sanitizeLabel;
            std::unordered_map<std::string, std::string> bbMap;
            bbMap.reserve(mir.blocks.size());
            const bool uniquify = (mod.functions.size() > 1);
            // Build map and rename blocks
            for (auto &bb : mir.blocks)
            {
                const std::string old = bb.name;
                const std::string suffix = uniquify ? (std::string("_") + fn.name) : std::string();
                const std::string neu = sanitizeLabel(old, suffix);
                bbMap.emplace(old, neu);
                bb.name = neu;
            }
            // Remap label operands for branches that target basic blocks
            for (auto &bb : mir.blocks)
            {
                for (auto &mi : bb.instrs)
                {
                    auto remapIfBB = [&](std::string &lbl)
                    {
                        auto it = bbMap.find(lbl);
                        if (it != bbMap.end())
                            lbl = it->second;
                    };
                    switch (mi.opc)
                    {
                        case viper::codegen::aarch64::MOpcode::Br:
                            if (mi.ops.size() >= 1 &&
                                mi.ops[0].kind == viper::codegen::aarch64::MOperand::Kind::Label)
                                remapIfBB(mi.ops[0].label);
                            break;
                        case viper::codegen::aarch64::MOpcode::BCond:
                            if (mi.ops.size() >= 2 &&
                                mi.ops[1].kind == viper::codegen::aarch64::MOperand::Kind::Label)
                                remapIfBB(mi.ops[1].label);
                            break;
                        default:
                            break;
                    }
                }
            }
        }
        // Remap labels in MIR that refer to IL string globals to pooled labels
        for (auto &bb : mir.blocks)
        {
            for (auto &mi : bb.instrs)
            {
                switch (mi.opc)
                {
                    case viper::codegen::aarch64::MOpcode::AdrPage:
                        if (mi.ops.size() >= 2 &&
                            mi.ops[1].kind == viper::codegen::aarch64::MOperand::Kind::Label)
                        {
                            const auto &n2l = pool.nameToLabel();
                            auto it = n2l.find(mi.ops[1].label);
                            if (it != n2l.end())
                                mi.ops[1].label = it->second;
                        }
                        break;
                    case viper::codegen::aarch64::MOpcode::AddPageOff:
                        if (mi.ops.size() >= 3 &&
                            mi.ops[2].kind == viper::codegen::aarch64::MOperand::Kind::Label)
                        {
                            const auto &n2l = pool.nameToLabel();
                            auto it = n2l.find(mi.ops[2].label);
                            if (it != n2l.end())
                                mi.ops[2].label = it->second;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
        // Detect leaf functions: scan for Bl/Blr instructions.
        mir.isLeaf = true;
        for (const auto &bb : mir.blocks)
        {
            for (const auto &mi : bb.instrs)
            {
                if (mi.opc == viper::codegen::aarch64::MOpcode::Bl ||
                    mi.opc == viper::codegen::aarch64::MOpcode::Blr)
                {
                    mir.isLeaf = false;
                    break;
                }
            }
            if (!mir.isLeaf)
                break;
        }

        // Dump MIR before register allocation if requested
        if (opts.dump_mir_before_ra)
        {
            std::cerr << "=== MIR before RA: " << fn.name << " ===\n";
            std::cerr << toString(mir) << "\n";
        }
        [[maybe_unused]] auto ra = allocate(mir, ti);
        // Dump MIR after register allocation if requested
        if (opts.dump_mir_after_ra)
        {
            std::cerr << "=== MIR after RA: " << fn.name << " ===\n";
            std::cerr << toString(mir) << "\n";
        }
        // Run peephole optimizations after RA
        [[maybe_unused]] auto peepholeStats = runPeephole(mir);
        // Debug: dump MIR after peephole
        if (opts.dump_mir_after_ra)
        {
            std::cerr << "=== MIR after peephole: " << fn.name << " ===\n";
            std::cerr << toString(mir) << "\n";
        }
        emitter.emitFunction(asmStream, mir);
        asmStream << "\n";
    }

    std::string asmText = asmStream.str();

    // Determine assembly destination
    std::string asmPath;
    if (opts.output_s)
        asmPath = *opts.output_s;
    else
    {
        // Derive path next to input: replace extension with .s
        std::filesystem::path p(opts.input_il);
        p.replace_extension(".s");
        asmPath = p.string();
    }
    // Apply Darwin symbol fixups only when assembling/linking native output.
#if defined(__APPLE__)
    if (opts.output_o || opts.run_native)
    {
        auto replace_all = [](std::string &hay, const std::string &from, const std::string &to)
        {
            std::size_t pos = 0;
            while ((pos = hay.find(from, pos)) != std::string::npos)
            {
                hay.replace(pos, from.size(), to);
                pos += to.size();
            }
        };
        // Always rewrite `main` to `_main` for Darwin toolchain
        replace_all(asmText, "\n.globl main\n", "\n.globl _main\n");
        replace_all(asmText, "\nmain:\n", "\n_main:\n");
        // Limit function remap to L*-prefixed names which cannot be global on Darwin
        for (const auto &fn : mod.functions)
        {
            const std::string &name = fn.name;
            if (name == "main")
                continue;
            const bool startsWithL = (!name.empty() && name[0] == 'L');
            if (!startsWithL)
                continue;
            replace_all(asmText,
                        std::string(".globl ") + name + "\n",
                        std::string(".globl _") + name + "\n");
            replace_all(
                asmText, std::string("\n") + name + ":\n", std::string("\n_") + name + ":\n");
            replace_all(
                asmText, std::string(" bl ") + name + "\n", std::string(" bl _") + name + "\n");
        }
        // Remap common runtime calls when producing a native object/binary
        const char *runtime_funcs[] = {"rt_trap",
                                       "rt_str_concat",
                                       "rt_print",
                                       "rt_input",
                                       "rt_malloc",
                                       "rt_free",
                                       "rt_memcpy",
                                       "rt_memset",
                                       "rt_const_cstr",
                                       "rt_print_str"};
        for (const char *rtfn : runtime_funcs)
        {
            replace_all(
                asmText, std::string(" bl ") + rtfn + "\n", std::string(" bl _") + rtfn + "\n");
        }
        // Prefix underscores for externs referenced by name (e.g., Viper.Console.PrintStr)
        for (const auto &ex : mod.externs)
        {
            // Skip rt_* already handled above
            if (ex.name.rfind("rt_", 0) == 0)
                continue;
            // Only map when an explicit call site is present
            const std::string from = std::string(" bl ") + ex.name + "\n";
            // Map Viper.Console.* to their rt_* equivalents when possible
            if (ex.name.rfind("Viper.Console.", 0) == 0)
            {
                const std::string suffix = ex.name.substr(std::string("Viper.Console.").size());
                std::string rt_equiv;
                if (suffix == "PrintStr")
                    rt_equiv = "rt_print_str";
                else if (suffix == "PrintI64")
                    rt_equiv = "rt_print_i64";
                else if (suffix == "PrintF64")
                    rt_equiv = "rt_print_f64";
                if (!rt_equiv.empty())
                    replace_all(asmText, from, std::string(" bl _") + rt_equiv + "\n");
                else
                    replace_all(asmText, from, std::string(" bl _") + ex.name + "\n");
            }
            else
            {
                replace_all(asmText, from, std::string(" bl _") + ex.name + "\n");
            }
        }
        // Generic safety net: prefix any remaining direct runtime calls (rt_*)
        replace_all(asmText, " bl rt_", " bl _rt_");
    }
#endif

    if (!writeTextFile(asmPath, asmText, std::cerr))
        return 1;

    // If only -S requested and no -o/-run-native, stop here.
    if (!opts.output_o && !opts.run_native)
        return 0;

    // If -o is provided and not run_native, assemble to object/exe depending on suffix
    if (opts.output_o && !opts.run_native)
    {
        // If output ends with .o, assemble to object; else produce an executable
        const std::string &outPath = *opts.output_o;
        if (std::filesystem::path(outPath).extension() == ".o")
        {
            return viper::codegen::common::invokeAssembler(
                       {"cc", "-arch", "arm64"}, asmPath, outPath, std::cout, std::cerr) == 0
                       ? 0
                       : 1;
        }
        // Link directly to executable
        const int lrc = linkToExe(asmPath, outPath, std::cout, std::cerr);
        if (lrc == 0 && !opts.emit_asm)
        {
            std::error_code ec;
            std::filesystem::remove(asmPath, ec);
        }
        return lrc == 0 ? 0 : 1;
    }

    // Otherwise, link to a default executable path and run (or just link if run_native=false)
    std::filesystem::path exe = opts.output_o
                                    ? std::filesystem::path(*opts.output_o)
                                    : std::filesystem::path(opts.input_il).replace_extension("");
    if (exe.extension().empty())
    {
        // Keep as derived without extension
    }
    if (linkToExe(asmPath, exe.string(), std::cout, std::cerr) != 0)
        return 1;

    // Clean up intermediate assembly file after successful linking.
    if (!opts.emit_asm)
    {
        std::error_code ec;
        std::filesystem::remove(asmPath, ec);
    }

    if (!opts.run_native)
        return 0;
    const int rc = viper::codegen::common::runExecutable(exe.string(), std::cout, std::cerr);
    return rc == -1 ? 1 : rc;
}

// condForOpcode mapping moved to LowerILToMIR.

} // namespace

/// @brief CLI entry point for `ilc codegen arm64`.
/// @details Parses argv-style options and delegates to
///          @ref emitAndMaybeLink for the actual code generation pipeline.
/// @param argc Number of arguments in @p argv.
/// @param argv Argument vector including the input IL path and flags.
/// @return Zero on success; non-zero on parsing or codegen failure.
int cmd_codegen_arm64(int argc, char **argv)
{
    const ArgvView args{argc, argv};
    auto parsed = parseArgs(args);
    if (!parsed)
        return 1;
    return emitAndMaybeLink(*parsed);
}

} // namespace viper::tools::ilc
