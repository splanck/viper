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

#include "cmd_codegen_arm64.hpp"

#include "codegen/aarch64/AsmEmitter.hpp"
#include "codegen/aarch64/LowerILToMIR.hpp"
#include "codegen/aarch64/RegAllocLinear.hpp"
#include "codegen/aarch64/RodataPool.hpp"
#include "codegen/common/ArgNormalize.hpp"
#include "codegen/common/LabelUtil.hpp"
#include "common/RunProcess.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
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

constexpr std::string_view kUsage =
    "usage: ilc codegen arm64 <file.il> [-S <file.s>] [-o <a.out>] [-run-native]\n";

struct ArgvView
{
    int argc;
    char **argv;

    [[nodiscard]] bool empty() const
    {
        return argc <= 0 || argv == nullptr;
    }

    [[nodiscard]] std::string_view front() const
    {
        return empty() ? std::string_view{} : argv[0];
    }

    [[nodiscard]] std::string_view at(int i) const
    {
        if (i < 0 || i >= argc || argv == nullptr)
            return std::string_view{};
        return argv[i];
    }

    [[nodiscard]] ArgvView drop_front(int n = 1) const
    {
        if (n >= argc)
            return {0, nullptr};
        return {argc - n, argv + n};
    }
};

struct Options
{
    std::string input_il;
    std::optional<std::string> output_s; // when set, always write assembly here
    std::optional<std::string> output_o; // when set without -run-native, emit object/exe here
    bool emit_asm = false;               // true when -S provided
    bool run_native = false;             // execute after linking
};

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
        std::cerr << "error: unknown flag '" << tok << "'\n" << kUsage;
        return std::nullopt;
    }
    return opts;
}

// Emit module-level string constants using a pooled, assembler-safe scheme
static void emitGlobalsAArch64(std::ostream &os, const viper::codegen::aarch64::RodataPool &pool)
{
    pool.emit(os);
}

// Minimal helpers adapted from x64 pipeline
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

static int assembleToObj(const std::string &asmPath,
                         const std::string &objPath,
                         std::ostream &out,
                         std::ostream &err)
{
    const RunResult rr = run_process({"cc", "-arch", "arm64", "-c", asmPath, "-o", objPath});
    if (rr.exit_code == -1)
    {
        err << "error: failed to launch system assembler command\n";
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

static int linkToExe(const std::string &asmPath,
                     const std::string &exePath,
                     std::ostream &out,
                     std::ostream &err)
{
    // Link against the built runtime static library to satisfy rt_* calls.
    // When executed under CTest, cwd is the build directory, so the runtime
    // archive is reachable at src/runtime/libviper_runtime.a.
    auto try_link = [&](const char *libPath) -> RunResult
    { return run_process({"cc", "-arch", "arm64", asmPath, libPath, "-o", exePath}); };
    RunResult rr = try_link("src/runtime/libviper_runtime.a");
    if (rr.exit_code != 0)
    {
        // Fallback for tests whose CWD is build/src/tests
        rr = try_link("../runtime/libviper_runtime.a");
    }
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

static int runExe(const std::string &exePath, std::ostream &out, std::ostream &err)
{
    const RunResult rr = run_process({exePath});
    if (rr.exit_code == -1)
    {
        err << "error: failed to execute '" << exePath << "'\n";
        return -1;
    }
    if (!rr.out.empty())
        out << rr.out;
#if defined(_WIN32)
    if (!rr.err.empty())
        err << rr.err;
#endif
    return rr.exit_code;
}

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

    using namespace viper::codegen::aarch64;

    // Host gating for --run-native: only allow on macOS arm64
    if (opts.run_native)
    {
#if !(defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__)))
        std::cerr << "error: --run-native is only supported on macOS arm64 hosts\n";
        return 1;
#endif
    }
    auto &ti = darwinTarget();
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
        [[maybe_unused]] auto ra = allocate(mir, ti);
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
                                       "rt_concat",
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
            return assembleToObj(asmPath, outPath, std::cout, std::cerr) == 0 ? 0 : 1;
        }
        // Link directly to executable
        return linkToExe(asmPath, outPath, std::cout, std::cerr) == 0 ? 0 : 1;
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
    if (!opts.run_native)
        return 0;
    const int rc = runExe(exe.string(), std::cout, std::cerr);
    return rc == -1 ? 1 : rc;
}

// condForOpcode mapping moved to LowerILToMIR.

} // namespace

int cmd_codegen_arm64(int argc, char **argv)
{
    const ArgvView args{argc, argv};
    auto parsed = parseArgs(args);
    if (!parsed)
        return 1;
    return emitAndMaybeLink(*parsed);
}

} // namespace viper::tools::ilc
