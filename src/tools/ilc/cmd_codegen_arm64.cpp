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
#include "codegen/common/ArgNormalize.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "tools/common/module_loader.hpp"
#include "common/RunProcess.hpp"

#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <filesystem>

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

// A minimal rodata pool for AArch64 string literals that
// - Deduplicates identical byte sequences
// - Assigns assembler-safe labels
// - Provides a mapping from IL global names to pooled labels so that
//   MIR label operands can be rewritten to the pooled symbol.
struct RodataPoolAArch64
{
    // Content -> pooled label
    std::unordered_map<std::string, std::string> contentToLabel;
    // IL global name -> pooled label
    std::unordered_map<std::string, std::string> nameToLabel;
    // Emission order for unique contents
    std::vector<std::pair<std::string, std::string>> ordered; // {label, content}

    static std::string makeSafeLabelForIndex(std::size_t idx)
    {
        // Follow a style similar to clang on Darwin: "L.str.<n>"
        // Keep it valid for both Mach-O and ELF toolchains.
        return std::string("L.str.") + std::to_string(idx);
    }

    static std::string escapeAsciz(std::string_view bytes)
    {
        std::string s;
        s.reserve(bytes.size());
        for (unsigned char c : bytes)
        {
            switch (c)
            {
                case '"':
                case '\\': s.push_back('\\'); s.push_back(static_cast<char>(c)); break;
                case '\n': s += "\\n"; break;
                case '\t': s += "\\t"; break;
                default:
                    if (c >= 32 && c < 127)
                    {
                        s.push_back(static_cast<char>(c));
                    }
                    else
                    {
                        char buf[5];
                        std::snprintf(buf, sizeof(buf), "\\x%02X", c);
                        s += buf;
                    }
            }
        }
        return s;
    }

    void addString(const std::string &ilName, const std::string &bytes)
    {
        // Dedup by content; assign stable label index based on insertion order
        auto it = contentToLabel.find(bytes);
        if (it == contentToLabel.end())
        {
            const std::string label = makeSafeLabelForIndex(ordered.size());
            contentToLabel.emplace(bytes, label);
            ordered.emplace_back(label, bytes);
            nameToLabel[ilName] = label;
        }
        else
        {
            nameToLabel[ilName] = it->second;
        }
    }

    void buildFromModule(const il::core::Module &mod)
    {
        for (const auto &g : mod.globals)
        {
            if (g.type.kind == il::core::Type::Kind::Str)
            {
                addString(g.name, g.init);
            }
        }
    }

    void emit(std::ostream &os) const
    {
        if (ordered.empty())
            return;
#if defined(__APPLE__)
        os << ".section __TEXT,__const\n";
#else
        os << ".section .rodata\n";
#endif
        for (const auto &pair : ordered)
        {
            const std::string &label = pair.first;
            const std::string &bytes = pair.second;
            os << label << ":\n";
            os << "  .asciz \"" << escapeAsciz(bytes) << "\"\n";
        }
        os << "\n";
    }
};

// Emit module-level string constants using a pooled, assembler-safe scheme
static void emitGlobalsAArch64(std::ostream &os, const RodataPoolAArch64 &pool)
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

static int assembleToObj(const std::string &asmPath, const std::string &objPath,
                         std::ostream &out, std::ostream &err)
{
    const RunResult rr = run_process({"cc", "-arch", "arm64", "-c", asmPath, "-o", objPath});
    if (rr.exit_code == -1)
    {
        err << "error: failed to launch system assembler command\n";
        return -1;
    }
    if (!rr.out.empty()) out << rr.out;
#if defined(_WIN32)
    if (!rr.err.empty()) err << rr.err;
#endif
    return rr.exit_code == 0 ? 0 : 1;
}

static int linkToExe(const std::string &asmPath, const std::string &exePath,
                     std::ostream &out, std::ostream &err)
{
    const RunResult rr = run_process({"cc", "-arch", "arm64", asmPath, "-o", exePath});
    if (rr.exit_code == -1)
    {
        err << "error: failed to launch system linker command\n";
        return -1;
    }
    if (!rr.out.empty()) out << rr.out;
#if defined(_WIN32)
    if (!rr.err.empty()) err << rr.err;
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
    if (!rr.out.empty()) out << rr.out;
#if defined(_WIN32)
    if (!rr.err.empty()) err << rr.err;
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
    auto &ti = darwinTarget();
    AsmEmitter emitter{ti};
    LowerILToMIR lowerer{ti};

    // Build a pooled view of rodata and emit at file start
    RodataPoolAArch64 pool;
    pool.buildFromModule(mod);

    std::ostringstream asmStream;
    emitGlobalsAArch64(asmStream, pool);
    for (const auto &fn : mod.functions)
    {
        MFunction mir = lowerer.lowerFunction(fn);
        // 1) Sanitize basic block labels and (optionally) uniquify across module
        {
            std::unordered_map<std::string, std::string> bbMap;
            bbMap.reserve(mir.blocks.size());
            const bool uniquify = (mod.functions.size() > 1);
            auto sanitize = [](std::string_view in) -> std::string {
                std::string out;
                out.reserve(in.size() + 2);
                for (unsigned char ch : in)
                {
                    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '$')
                    {
                        out.push_back(static_cast<char>(ch));
                    }
                    else if (ch == '-')
                    {
                        // Drop '-' (e.g., from negative numeric fragments)
                    }
                    else
                    {
                        out.push_back('_');
                    }
                }
                if (out.empty() || (out[0] >= '0' && out[0] <= '9'))
                    out.insert(out.begin(), 'L');
                return out;
            };
            // Build map and rename blocks
            for (auto &bb : mir.blocks)
            {
                const std::string old = bb.name;
                std::string neu = sanitize(old);
                if (uniquify)
                {
                    // Avoid colliding with function symbol and other functions' labels
                    neu += "_";
                    neu += fn.name;
                }
                bbMap.emplace(old, neu);
                bb.name = neu;
            }
            // Remap label operands for branches that target basic blocks
            for (auto &bb : mir.blocks)
            {
                for (auto &mi : bb.instrs)
                {
                    auto remapIfBB = [&](std::string &lbl) {
                        auto it = bbMap.find(lbl);
                        if (it != bbMap.end()) lbl = it->second;
                    };
                    switch (mi.opc)
                    {
                        case viper::codegen::aarch64::MOpcode::Br:
                            if (mi.ops.size() >= 1 && mi.ops[0].kind == viper::codegen::aarch64::MOperand::Kind::Label)
                                remapIfBB(mi.ops[0].label);
                            break;
                        case viper::codegen::aarch64::MOpcode::BCond:
                            if (mi.ops.size() >= 2 && mi.ops[1].kind == viper::codegen::aarch64::MOperand::Kind::Label)
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
                        if (mi.ops.size() >= 2 && mi.ops[1].kind == viper::codegen::aarch64::MOperand::Kind::Label)
                        {
                            auto it = pool.nameToLabel.find(mi.ops[1].label);
                            if (it != pool.nameToLabel.end())
                                mi.ops[1].label = it->second;
                        }
                        break;
                    case viper::codegen::aarch64::MOpcode::AddPageOff:
                        if (mi.ops.size() >= 3 && mi.ops[2].kind == viper::codegen::aarch64::MOperand::Kind::Label)
                        {
                            auto it = pool.nameToLabel.find(mi.ops[2].label);
                            if (it != pool.nameToLabel.end())
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
#if defined(__APPLE__)
    // On Darwin, remap function symbols to the leading-underscore form expected by the toolchain.
    auto replace_all = [](std::string &hay, const std::string &from, const std::string &to) {
        std::size_t pos = 0;
        while ((pos = hay.find(from, pos)) != std::string::npos)
        {
            hay.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    // Always rewrite `main` to `_main`.
    replace_all(asmText, "\n.globl main\n", "\n.globl _main\n");
    replace_all(asmText, "\nmain:\n", "\n_main:\n");
    // Rewrite all other function definitions and call sites.
    for (const auto &fn : mod.functions)
    {
        const std::string &name = fn.name;
        if (name == "main")
            continue;
        const bool startsWithL = (!name.empty() && name[0] == 'L');
        if (!startsWithL)
            continue; // Limit Darwin underscore remap to problematic L*-prefixed names
        // Definition sites
        replace_all(asmText, std::string(".globl ") + name + "\n",
                    std::string(".globl _") + name + "\n");
        replace_all(asmText, std::string("\n") + name + ":\n",
                    std::string("\n_") + name + ":\n");
        // Call sites (conservative: space-prefixed opcode pattern)
        replace_all(asmText, std::string(" bl ") + name + "\n",
                    std::string(" bl _") + name + "\n");
        // Also handle potential labels used in ADRP/ADD page relocations (globals unlikely here)
    }
#endif

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
    std::filesystem::path exe = opts.output_o ? std::filesystem::path(*opts.output_o)
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
