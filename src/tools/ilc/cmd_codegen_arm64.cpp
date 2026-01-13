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
#include "codegen/common/ArgNormalize.hpp"
#include "codegen/common/LabelUtil.hpp"
#include "common/RunProcess.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "tools/common/module_loader.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

/// @brief Lightweight argv view with bounds-checked helpers.
/// @details Wraps the raw argc/argv pair to make argument parsing more explicit
///          and defensive. The view does not own the underlying strings; it
///          merely provides safe accessors and slicing helpers.
struct ArgvView
{
    int argc;
    char **argv;

    /// @brief Report whether the view contains any usable arguments.
    /// @return True if argc is zero or argv is null; otherwise false.
    [[nodiscard]] bool empty() const
    {
        return argc <= 0 || argv == nullptr;
    }

    /// @brief Return the first argument if present.
    /// @return argv[0] when available; otherwise an empty string view.
    [[nodiscard]] std::string_view front() const
    {
        return empty() ? std::string_view{} : argv[0];
    }

    /// @brief Fetch an argument by index with bounds checks.
    /// @param i Zero-based argument index to query.
    /// @return argv[i] if in range; otherwise an empty string view.
    [[nodiscard]] std::string_view at(int i) const
    {
        if (i < 0 || i >= argc || argv == nullptr)
            return std::string_view{};
        return argv[i];
    }

    /// @brief Create a view with the first @p n arguments removed.
    /// @param n Count of arguments to drop from the front.
    /// @return New ArgvView starting at argv[n], or an empty view if @p n exceeds
    ///         the current argument count.
    [[nodiscard]] ArgvView drop_front(int n = 1) const
    {
        if (n >= argc)
            return {0, nullptr};
        return {argc - n, argv + n};
    }
};

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

/// @brief Assemble an AArch64 assembly file into an object file.
/// @details Invokes the system C compiler with `-arch arm64 -c` to assemble
///          @p asmPath into @p objPath. Standard output is forwarded to @p out,
///          and stderr is forwarded on Windows where the toolchain uses it.
/// @param asmPath Path to the input `.s` file.
/// @param objPath Output `.o` path.
/// @param out Stream receiving tool output on success.
/// @param err Stream receiving tool diagnostics on failure.
/// @return 0 on success, 1 on assembler failure, -1 if the tool could not launch.
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

/// @brief Link assembly into a native executable, adding runtime archives as needed.
/// @details Scans the emitted assembly for referenced runtime symbols, selects
///          the minimal set of runtime archives to link, and (when a build
///          directory is available) triggers a cmake build for missing archives.
///          The resulting `cc` invocation links in dependency order and applies
///          platform-specific flags for dead code elimination and graphics
///          frameworks. Diagnostics are sent to @p err and summarized via
///          the returned status code.
/// @param asmPath Path to the generated assembly file.
/// @param exePath Destination path for the linked executable.
/// @param out Stream receiving linker output on success.
/// @param err Stream receiving linker diagnostics on failure.
/// @return 0 on success, 1 on link failure, -1 if the tool could not launch.
static int linkToExe(const std::string &asmPath,
                     const std::string &exePath,
                     std::ostream &out,
                     std::ostream &err)
{
    auto file_exists = [](const std::filesystem::path &path) -> bool
    {
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    };

    auto read_file = [&](const std::filesystem::path &path, std::string &dst) -> bool
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        dst = ss.str();
        return true;
    };

    auto find_build_dir = [&]() -> std::optional<std::filesystem::path>
    {
        std::error_code ec;
        std::filesystem::path cur = std::filesystem::current_path(ec);
        if (!ec)
        {
            for (int depth = 0; depth < 8; ++depth)
            {
                if (file_exists(cur / "CMakeCache.txt"))
                    return cur;
                if (!cur.has_parent_path())
                    break;
                cur = cur.parent_path();
            }
        }

        // Fallback for running from repo root with the default build directory.
        const std::filesystem::path defaultBuild = std::filesystem::path("build");
        if (file_exists(defaultBuild / "CMakeCache.txt"))
            return defaultBuild;

        return std::nullopt;
    };

    auto parse_runtime_symbols = [](std::string_view text) -> std::unordered_set<std::string>
    {
        auto is_ident = [](unsigned char c) -> bool { return std::isalnum(c) || c == '_'; };

        std::unordered_set<std::string> symbols;
        for (std::size_t i = 0; i + 3 < text.size(); ++i)
        {
            std::size_t start = std::string_view::npos;
            std::size_t boundary = std::string_view::npos;
            if (text[i] == 'r' && text[i + 1] == 't' && text[i + 2] == '_')
            {
                start = i;
                boundary = (start == 0) ? std::string_view::npos : (start - 1);
            }
            else if (text[i] == '_' && text[i + 1] == 'r' && text[i + 2] == 't' &&
                     text[i + 3] == '_')
            {
                start = i + 1;
                boundary = (i == 0) ? std::string_view::npos : (i - 1);
            }

            if (start == std::string_view::npos)
                continue;
            if (boundary != std::string_view::npos &&
                is_ident(static_cast<unsigned char>(text[boundary])))
                continue;

            std::size_t j = start;
            while (j < text.size() && is_ident(static_cast<unsigned char>(text[j])))
                ++j;

            if (j > start)
                symbols.emplace(text.substr(start, j - start));
            i = j;
        }
        return symbols;
    };

    enum class RtComponent
    {
        Base,
        Arrays,
        Oop,
        Collections,
        Text,
        IoFs,
        Exec,
        Threads,
        Graphics,
    };

    auto needs_component_for_symbol = [](std::string_view sym) -> std::optional<RtComponent>
    {
        auto starts = [&](std::string_view p) -> bool { return sym.rfind(p, 0) == 0; };

        if (starts("rt_arr_"))
            return RtComponent::Arrays;

        if (starts("rt_obj_") || starts("rt_type_") || starts("rt_cast_") || starts("rt_ns_") ||
            sym == "rt_bind_interface")
            return RtComponent::Oop;

        if (starts("rt_list_") || starts("rt_map_") || starts("rt_treemap_") || starts("rt_bag_") ||
            starts("rt_queue_") || starts("rt_ring_") || starts("rt_seq_") || starts("rt_stack_") ||
            starts("rt_bytes_"))
            return RtComponent::Collections;

        if (starts("rt_codec_") || starts("rt_csv_") || starts("rt_guid_") || starts("rt_hash_") ||
            starts("rt_parse_"))
            return RtComponent::Text;

        if (starts("rt_file_") || starts("rt_dir_") || starts("rt_path_") ||
            starts("rt_binfile_") || starts("rt_linereader_") || starts("rt_linewriter_") ||
            starts("rt_io_file_") || sym == "rt_eof_ch" || sym == "rt_lof_ch" ||
            sym == "rt_loc_ch" || sym == "rt_close_err" || sym == "rt_seek_ch_err" ||
            sym == "rt_write_ch_err" || sym == "rt_println_ch_err" ||
            sym == "rt_line_input_ch_err" || sym == "rt_open_err_vstr")
            return RtComponent::IoFs;

        if (starts("rt_exec_") || starts("rt_machine_"))
            return RtComponent::Exec;

        if (starts("rt_monitor_") || starts("rt_thread_") || starts("rt_safe_"))
            return RtComponent::Threads;

        if (starts("rt_canvas_") || starts("rt_color_") || starts("rt_vec2_") ||
            starts("rt_vec3_") || starts("rt_pixels_"))
            return RtComponent::Graphics;

        return std::nullopt;
    };

    std::string asmText;
    if (!read_file(asmPath, asmText))
    {
        err << "error: unable to read '" << asmPath << "' for runtime library selection\n";
        return 1;
    }

    const std::unordered_set<std::string> symbols = parse_runtime_symbols(asmText);

    bool needArrays = false;
    bool needOop = false;
    bool needCollections = false;
    bool needText = false;
    bool needIoFs = false;
    bool needExec = false;
    bool needThreads = false;
    bool needGraphics = false;

    for (const auto &sym : symbols)
    {
        const auto comp = needs_component_for_symbol(sym);
        if (!comp)
            continue;
        switch (*comp)
        {
            case RtComponent::Arrays:
                needArrays = true;
                break;
            case RtComponent::Oop:
                needOop = true;
                break;
            case RtComponent::Collections:
                needCollections = true;
                break;
            case RtComponent::Text:
                needText = true;
                break;
            case RtComponent::IoFs:
                needIoFs = true;
                break;
            case RtComponent::Exec:
                needExec = true;
                break;
            case RtComponent::Threads:
                needThreads = true;
                break;
            case RtComponent::Graphics:
                needGraphics = true;
                break;
            case RtComponent::Base:
                break;
        }
    }

    // Component dependencies (internal runtime calls).
    if (needText || needIoFs || needExec)
        needCollections = true;
    if (needCollections || needArrays || needGraphics || needThreads)
        needOop = true;

    const std::optional<std::filesystem::path> buildDirOpt = find_build_dir();
    const std::filesystem::path buildDir = buildDirOpt.value_or(std::filesystem::path{});

    auto runtime_archive_path = [&](std::string_view libBaseName) -> std::filesystem::path
    {
        if (!buildDir.empty())
            return buildDir / "src/runtime" /
                   (std::string("lib") + std::string(libBaseName) + ".a");
        return std::filesystem::path("src/runtime") /
               (std::string("lib") + std::string(libBaseName) + ".a");
    };

    std::vector<std::pair<std::string, std::filesystem::path>> requiredArchives;
    requiredArchives.emplace_back("viper_rt_base", runtime_archive_path("viper_rt_base"));
    if (needOop)
        requiredArchives.emplace_back("viper_rt_oop", runtime_archive_path("viper_rt_oop"));
    if (needArrays)
        requiredArchives.emplace_back("viper_rt_arrays", runtime_archive_path("viper_rt_arrays"));
    if (needCollections)
        requiredArchives.emplace_back("viper_rt_collections",
                                      runtime_archive_path("viper_rt_collections"));
    if (needText)
        requiredArchives.emplace_back("viper_rt_text", runtime_archive_path("viper_rt_text"));
    if (needIoFs)
        requiredArchives.emplace_back("viper_rt_io_fs", runtime_archive_path("viper_rt_io_fs"));
    if (needExec)
        requiredArchives.emplace_back("viper_rt_exec", runtime_archive_path("viper_rt_exec"));
    if (needThreads)
        requiredArchives.emplace_back("viper_rt_threads", runtime_archive_path("viper_rt_threads"));
    if (needGraphics)
        requiredArchives.emplace_back("viper_rt_graphics",
                                      runtime_archive_path("viper_rt_graphics"));

    std::vector<std::string> missingTargets;
    if (!buildDir.empty())
    {
        for (const auto &[tgt, path] : requiredArchives)
        {
            if (!file_exists(path))
                missingTargets.push_back(tgt);
        }
        if (needGraphics)
        {
            const std::filesystem::path gfxLib = buildDir / "lib" / "libvipergfx.a";
            if (!file_exists(gfxLib))
                missingTargets.push_back("vipergfx");
        }
        if (!missingTargets.empty())
        {
            std::vector<std::string> cmd = {"cmake", "--build", buildDir.string(), "--target"};
            cmd.insert(cmd.end(), missingTargets.begin(), missingTargets.end());
            const RunResult build = run_process(cmd);
            if (!build.out.empty())
                out << build.out;
#if defined(_WIN32)
            if (!build.err.empty())
                err << build.err;
#endif
            if (build.exit_code != 0)
            {
                err << "error: failed to build required runtime libraries in '" << buildDir.string()
                    << "'\n";
                return 1;
            }
        }
    }

    // Link order: dependents first, base last.
    std::vector<std::string> linkCmd = {"cc", "-arch", "arm64", asmPath};
    auto appendArchiveIf = [&](std::string_view name)
    {
        const std::filesystem::path path = runtime_archive_path(name);
        if (file_exists(path))
            linkCmd.push_back(path.string());
    };

    if (needGraphics)
        appendArchiveIf("viper_rt_graphics");
    if (needExec)
        appendArchiveIf("viper_rt_exec");
    if (needIoFs)
        appendArchiveIf("viper_rt_io_fs");
    if (needText)
        appendArchiveIf("viper_rt_text");
    if (needCollections)
        appendArchiveIf("viper_rt_collections");
    if (needArrays)
        appendArchiveIf("viper_rt_arrays");
    if (needThreads)
        appendArchiveIf("viper_rt_threads");
    if (needOop)
        appendArchiveIf("viper_rt_oop");
    appendArchiveIf("viper_rt_base");

    if (needGraphics)
    {
        std::filesystem::path gfxLib;
        if (!buildDir.empty())
            gfxLib = buildDir / "lib" / "libvipergfx.a";
        else
            gfxLib = std::filesystem::path("lib") / "libvipergfx.a";
        if (file_exists(gfxLib))
            linkCmd.push_back(gfxLib.string());
#if defined(__APPLE__)
        linkCmd.push_back("-framework");
        linkCmd.push_back("Cocoa");
        linkCmd.push_back("-framework");
        linkCmd.push_back("IOKit");
        linkCmd.push_back("-framework");
        linkCmd.push_back("CoreFoundation");
#endif
    }

#if defined(__APPLE__)
    linkCmd.push_back("-Wl,-dead_strip");
#elif !defined(_WIN32)
    linkCmd.push_back("-Wl,--gc-sections");
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

/// @brief Execute a linked native binary and forward its output.
/// @details Runs the executable at @p exePath and forwards its stdout/stderr
///          to @p out/@p err so the CLI behaves similarly to the VM runner.
/// @param exePath Path to the executable to run.
/// @param out Stream receiving program stdout.
/// @param err Stream receiving program stderr or launcher diagnostics.
/// @return Exit code from the program, or -1 if launching failed.
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
