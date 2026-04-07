//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/CodegenPipeline.cpp
// Purpose: Implements the modular AArch64 code-generation pipeline by wiring
//          all AArch64 passes through the PassManager in the correct order and
//          providing a reusable end-to-end pipeline for the CLI.
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/CodegenPipeline.hpp"

#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"
#include "codegen/aarch64/passes/BinaryEmitPass.hpp"
#include "codegen/aarch64/passes/BlockLayoutPass.hpp"
#include "codegen/aarch64/passes/EmitPass.hpp"
#include "codegen/aarch64/passes/LoweringPass.hpp"
#include "codegen/aarch64/passes/PeepholePass.hpp"
#include "codegen/aarch64/passes/RegAllocPass.hpp"
#include "codegen/aarch64/passes/SchedulerPass.hpp"
#include "codegen/common/LinkerSupport.hpp"
#include "codegen/common/NativeEHLowering.hpp"
#include "codegen/common/linker/NativeLinker.hpp"
#include "codegen/common/objfile/ObjectFileWriter.hpp"
#include "common/RunProcess.hpp"
#include "il/transform/PassManager.hpp"
#include "tools/common/module_loader.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace viper::codegen::aarch64 {

namespace {

using viper::codegen::common::LinkContext;

/// @brief Dump all MIR functions to the provided stream with a header tag.
static void dumpMir(const passes::AArch64Module &module, const char *tag, std::ostream &os) {
    for (const auto &fn : module.mir) {
        os << "=== MIR " << tag << ": " << fn.name << " ===\n";
        os << toString(fn) << "\n";
    }
}

static std::vector<std::string> systemAssemblerArgs() {
#if defined(__APPLE__)
    return {"cc", "-arch", "arm64"};
#elif defined(_WIN32)
    return {"clang", "--target=aarch64-pc-windows-msvc"};
#else
    return {"cc"};
#endif
}

static int linkObjToExe(const std::string &objPath,
                        const std::string &exePath,
                        const LinkContext &ctx,
                        std::size_t stackSize,
                        const std::vector<std::string> &extraObjects,
                        std::ostream &out,
                        std::ostream &err);

static int linkToExe(const std::string &asmPath,
                     const std::string &exePath,
                     std::size_t stackSize,
                     const std::vector<std::string> &extraObjects,
                     std::ostream &out,
                     std::ostream &err) {
    using namespace viper::codegen::common;

    LinkContext ctx;
    if (const int rc = prepareLinkContext(asmPath, ctx, out, err); rc != 0)
        return rc;

    std::filesystem::path objPath = std::filesystem::path(asmPath);
    objPath.replace_extension(".o");
    const int arc = invokeAssembler(systemAssemblerArgs(), asmPath, objPath.string(), out, err);
    if (arc != 0)
        return arc;

    const int lrc = linkObjToExe(objPath.string(), exePath, ctx, stackSize, extraObjects, out, err);
    std::error_code ec;
    std::filesystem::remove(objPath, ec);
    return lrc;
}

static int linkObjToExe(const std::string &objPath,
                        const std::string &exePath,
                        const LinkContext &ctx,
                        std::size_t stackSize,
                        const std::vector<std::string> &extraObjects,
                        std::ostream &out,
                        std::ostream &err) {
    using namespace viper::codegen::common;
    using viper::codegen::archiveNameForComponent;
    using viper::codegen::RtComponent;

    linker::NativeLinkerOptions linkOpts;
    linkOpts.objPath = objPath;
    linkOpts.exePath = exePath;
    linkOpts.platform = linker::detectLinkPlatform();
    linkOpts.arch = linker::LinkArch::AArch64;
    linkOpts.entrySymbol = "main";
    linkOpts.stackSize = stackSize;
    linkOpts.extraObjPaths = extraObjects;

    static constexpr RtComponent allComponents[] = {
        RtComponent::Network,
        RtComponent::Threads,
        RtComponent::Audio,
        RtComponent::Graphics,
        RtComponent::Game,
        RtComponent::Exec,
        RtComponent::IoFs,
        RtComponent::Text,
        RtComponent::Collections,
        RtComponent::Arrays,
        RtComponent::Oop,
        RtComponent::Base,
    };
    for (auto comp : allComponents) {
        auto arPath = runtimeArchivePath(ctx.buildDir, archiveNameForComponent(comp));
        if (fileExists(arPath))
            linkOpts.archivePaths.push_back(arPath.string());
    }

    auto addIfExists = [&](const std::filesystem::path &p) {
        if (fileExists(p))
            linkOpts.archivePaths.push_back(p.string());
    };
    addIfExists(ctx.buildDir / "src" / "lib" / "gui" / "libvipergui.a");
    addIfExists(ctx.buildDir / "lib" / "libvipergfx.a");
    addIfExists(ctx.buildDir / "lib" / "libviperaud.a");

    return viper::codegen::linker::nativeLink(linkOpts, out, err);
}

static void applyDarwinAsmFixups(std::string &asmText, const il::core::Module &mod) {
    auto replace_all = [](std::string &hay, const std::string &from, const std::string &to) {
        std::size_t pos = 0;
        while ((pos = hay.find(from, pos)) != std::string::npos) {
            hay.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    replace_all(asmText, "\n.globl main\n", "\n.globl _main\n");
    replace_all(asmText, "\nmain:\n", "\n_main:\n");

    for (const auto &fn : mod.functions) {
        const std::string &name = fn.name;
        if (name == "main")
            continue;
        if (name.empty() || name[0] != 'L')
            continue;
        replace_all(
            asmText, std::string(".globl ") + name + "\n", std::string(".globl _") + name + "\n");
        replace_all(asmText, std::string("\n") + name + ":\n", std::string("\n_") + name + ":\n");
        replace_all(asmText, std::string(" bl ") + name + "\n", std::string(" bl _") + name + "\n");
    }

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
    for (const char *rtfn : runtime_funcs) {
        replace_all(asmText, std::string(" bl ") + rtfn + "\n", std::string(" bl _") + rtfn + "\n");
    }

    for (const auto &ex : mod.externs) {
        if (ex.name.rfind("rt_", 0) == 0)
            continue;

        const std::string from = std::string(" bl ") + ex.name + "\n";
        if (ex.name.rfind("Viper.Console.", 0) == 0) {
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
        } else {
            replace_all(asmText, from, std::string(" bl _") + ex.name + "\n");
        }
    }

    replace_all(asmText, " bl rt_", " bl _rt_");
}

static bool runIlOptimizations(il::core::Module &mod, int optimizeLevel) {
    if (optimizeLevel < 1)
        return true;

    auto hasEhSensitiveControl = [](const il::core::Module &module) {
        for (const auto &fn : module.functions) {
            for (const auto &bb : fn.blocks) {
                if (bb.params.size() >= 2 &&
                    bb.params[0].type.kind == il::core::Type::Kind::Error &&
                    bb.params[1].type.kind == il::core::Type::Kind::ResumeTok) {
                    return true;
                }

                for (const auto &instr : bb.instructions) {
                    switch (instr.op) {
                        case il::core::Opcode::EhPush:
                        case il::core::Opcode::EhPop:
                        case il::core::Opcode::EhEntry:
                        case il::core::Opcode::ResumeSame:
                        case il::core::Opcode::ResumeNext:
                        case il::core::Opcode::ResumeLabel:
                            return true;
                        default:
                            break;
                    }
                }
            }
        }
        return false;
    };

    if (hasEhSensitiveControl(mod)) {
        // The generic IL optimization pipelines still do not preserve all
        // handler/resume structural invariants. Keep O1/O2 as the CLI default,
        // but bypass unsafe IL rewrites for EH-bearing modules and rely on the
        // backend cleanup passes instead.
        return true;
    }

    constexpr std::size_t kLargeModuleIlOptThreshold = 100000;
    auto totalInstructionCount = [](const il::core::Module &module) {
        std::size_t totalInstrs = 0;
        for (const auto &fn : module.functions)
            for (const auto &bb : fn.blocks)
                totalInstrs += bb.instructions.size();
        return totalInstrs;
    };

    il::transform::PassManager ilpm;
    const std::size_t totalInstrs = totalInstructionCount(mod);
    if (totalInstrs > kLargeModuleIlOptThreshold) {
        // Huge frontend-built modules such as sqldb spend disproportionate
        // time in the IL optimizer. Skip IL-level optimization entirely here
        // and rely on backend codegen cleanup so native demo builds do not
        // appear hung for minutes.
        return true;
    }
    if (optimizeLevel >= 2) {
        ilpm.registerPipeline("codegen-O2",
                              {"loop-simplify", "loop-rotate",  "indvars",           "loop-unroll",
                               "simplify-cfg",  "sccp",         "check-opt",         "eh-opt",
                               "dce",           "simplify-cfg", "sibling-recursion", "inline",
                               "simplify-cfg",  "sccp",         "constfold",         "dce",
                               "simplify-cfg",  "gvn",          "reassociate",       "earlycse",
                               "dse",           "dce",          "late-cleanup"});
        return ilpm.runPipeline(mod, "codegen-O2");
    }

    ilpm.registerPipeline("codegen-O1",
                          {"simplify-cfg",
                           "sccp",
                           "constfold",
                           "dce",
                           "simplify-cfg",
                           "sccp",
                           "inline",
                           "dce",
                           "simplify-cfg"});
    return ilpm.runPipeline(mod, "codegen-O1");
}

static const TargetInfo &hostAArch64Target() {
#if defined(_WIN32)
    return windowsTarget();
#elif defined(__APPLE__)
    return darwinTarget();
#else
    return linuxTarget();
#endif
}

static const TargetInfo &selectAArch64Target(CodegenPipeline::TargetPlatform platform) {
    switch (platform) {
        case CodegenPipeline::TargetPlatform::Darwin:
            return darwinTarget();
        case CodegenPipeline::TargetPlatform::Linux:
            return linuxTarget();
        case CodegenPipeline::TargetPlatform::Windows:
            return windowsTarget();
        case CodegenPipeline::TargetPlatform::Host:
            return hostAArch64Target();
    }
    return hostAArch64Target();
}

} // namespace

/// @brief Run the full AArch64 codegen pipeline: lower → peephole → regalloc → schedule → emit.
/// @details Orchestrates all passes via PassManager in order: IL-to-MIR lowering,
///          peephole optimization, register allocation (with optional coalescing),
///          post-RA scheduling (O1+), block layout, and assembly/binary emission.
bool runCodegenPipeline(passes::AArch64Module &module,
                        const PipelineOptions &opts,
                        std::ostream &diagOut) {
    passes::Diagnostics diags;
    auto flushOnFailure = [&]() {
        diags.flush(diagOut);
        return false;
    };

    {
        passes::PassManager manager;
        manager.addPass(std::make_unique<passes::LoweringPass>());
        if (!manager.run(module, diags))
            return flushOnFailure();
    }

    if (opts.dumpMirBeforeRA)
        dumpMir(module, "before RA", diagOut);

    {
        passes::PassManager manager;
        manager.addPass(std::make_unique<passes::RegAllocPass>());
        if (!manager.run(module, diags))
            return flushOnFailure();
    }

    if (opts.dumpMirAfterRA)
        dumpMir(module, "after RA", diagOut);

    if (opts.optimizeLevel >= 1) {
        passes::PassManager manager;
        manager.addPass(std::make_unique<passes::SchedulerPass>());
        manager.addPass(std::make_unique<passes::BlockLayoutPass>());
        manager.addPass(std::make_unique<passes::PeepholePass>());
        if (!manager.run(module, diags))
            return flushOnFailure();
    }

    if (opts.dumpMirAfterRA && opts.optimizeLevel >= 1)
        dumpMir(module, "after peephole", diagOut);

    {
        passes::PassManager manager;
        if (opts.emitAssemblyText)
            manager.addPass(std::make_unique<passes::EmitPass>());
        if (opts.useBinaryEmit)
            manager.addPass(std::make_unique<passes::BinaryEmitPass>());
        if (!manager.run(module, diags))
            return flushOnFailure();
    }

    diags.flush(diagOut);
    return true;
}

CodegenPipeline::CodegenPipeline(Options opts) : opts_(std::move(opts)) {}

PipelineResult CodegenPipeline::run() {
    PipelineResult result{};
    std::ostringstream out;
    std::ostringstream err;

    il::core::Module mod;
    const auto load = il::tools::common::loadModuleFromFile(opts_.input_il_path, mod, err);
    if (!load.succeeded()) {
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }
    if (!il::tools::common::verifyModule(mod, err)) {
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    viper::codegen::common::lowerNativeEh(mod);

    if (!runIlOptimizations(mod, opts_.optimize)) {
        err << "error: failed to run AArch64 IL optimization pipeline\n";
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }
    if (opts_.optimize >= 1 && !il::tools::common::verifyModule(mod, err)) {
        err << "error: IL verification failed after optimization\n";
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    if (opts_.run_native) {
        if (opts_.target_platform != TargetPlatform::Host) {
            err << "error: --run-native requires --target-host on the AArch64 backend\n";
            result.exit_code = 1;
            result.stdout_text = out.str();
            result.stderr_text = err.str();
            return result;
        }
#if !(defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__)))
        err << "error: --run-native is only supported on macOS arm64 hosts\n";
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
#endif
    }

    const TargetInfo &ti = selectAArch64Target(opts_.target_platform);

    passes::AArch64Module pipelineModule;
    pipelineModule.ilMod = &mod;
    pipelineModule.ti = &ti;
    pipelineModule.debugSourcePath = opts_.input_il_path;

    PipelineOptions pipeOpts;
    pipeOpts.dumpMirBeforeRA = opts_.dump_mir_before_ra;
    pipeOpts.dumpMirAfterRA = opts_.dump_mir_after_ra;
    pipeOpts.emitAssemblyText = opts_.assembler_mode == AssemblerMode::System || opts_.emit_asm ||
                                (opts_.output_obj_path.empty() && !opts_.run_native);
    pipeOpts.useBinaryEmit = opts_.assembler_mode == AssemblerMode::Native;
    pipeOpts.optimizeLevel = opts_.optimize;

    if (!runCodegenPipeline(pipelineModule, pipeOpts, err)) {
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    std::string asmText = pipelineModule.assembly;
    std::string asmPath = opts_.output_asm_path;
    if (asmPath.empty()) {
        std::filesystem::path p(opts_.input_il_path);
        p.replace_extension(".s");
        asmPath = p.string();
    }

    if (ti.abiFormat == ABIFormat::Darwin && (!opts_.output_obj_path.empty() || opts_.run_native))
        applyDarwinAsmFixups(asmText, mod);

    if (opts_.emit_asm) {
        if (!common::writeTextFile(asmPath, asmText, err)) {
            result.exit_code = 1;
            result.stdout_text = out.str();
            result.stderr_text = err.str();
            return result;
        }
    }

    if (opts_.output_obj_path.empty() && !opts_.run_native) {
        if (!opts_.emit_asm && !common::writeTextFile(asmPath, asmText, err)) {
            result.exit_code = 1;
            result.stdout_text = out.str();
            result.stderr_text = err.str();
            return result;
        }
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    // --- Inject asset blob into .rodata (if present) ---
    if (pipelineModule.binaryRodata && !opts_.asset_blob_path.empty()) {
        std::ifstream af(opts_.asset_blob_path, std::ios::binary | std::ios::ate);
        if (af.is_open()) {
            auto blobSize = af.tellg();
            if (blobSize > 0) {
                af.seekg(0);
                std::vector<uint8_t> assetBlob(static_cast<size_t>(blobSize));
                af.read(reinterpret_cast<char *>(assetBlob.data()), blobSize);

                // AArch64 MachO writer adds _ prefix to global symbols automatically.
                auto &rodata = *pipelineModule.binaryRodata;
                rodata.alignTo(16);
                rodata.defineSymbol("viper_asset_blob",
                                    objfile::SymbolBinding::Global,
                                    objfile::SymbolSection::Rodata);
                rodata.emitBytes(assetBlob.data(), assetBlob.size());
                rodata.alignTo(8);
                rodata.defineSymbol("viper_asset_blob_size",
                                    objfile::SymbolBinding::Global,
                                    objfile::SymbolSection::Rodata);
                rodata.emit64LE(static_cast<uint64_t>(assetBlob.size()));
            }
        }
    }

    if (opts_.assembler_mode == AssemblerMode::Native && pipelineModule.binaryText) {
        std::filesystem::path objPath;
        bool outputIsObj = false;
        if (!opts_.output_obj_path.empty() &&
            std::filesystem::path(opts_.output_obj_path).extension() == ".o") {
            objPath = opts_.output_obj_path;
            outputIsObj = true;
        } else {
            objPath = std::filesystem::path(opts_.input_il_path).replace_extension(".o");
        }

        using namespace viper::codegen::objfile;
        auto writer = createObjectFileWriter(detectHostFormat(), ObjArch::AArch64);
        if (!writer) {
            err << "error: no native object file writer for this platform\n";
            result.exit_code = 1;
            result.stdout_text = out.str();
            result.stderr_text = err.str();
            return result;
        }
        if (!pipelineModule.debugLineData.empty())
            writer->setDebugLineData(std::move(pipelineModule.debugLineData));
        if (!writer->write(objPath.string(),
                           pipelineModule.binaryTextSections,
                           *pipelineModule.binaryRodata,
                           err)) {
            err << "error: failed to write object file '" << objPath.string() << "'\n";
            result.exit_code = 1;
            result.stdout_text = out.str();
            result.stderr_text = err.str();
            return result;
        }

        if (outputIsObj) {
            result.stdout_text = out.str();
            result.stderr_text = err.str();
            return result;
        }

        std::unordered_set<std::string> extSymbols;
        for (const auto &sym : pipelineModule.binaryText->symbols()) {
            if (sym.binding == viper::codegen::objfile::SymbolBinding::External)
                extSymbols.insert(sym.name);
        }

        LinkContext ctx;
        if (const int rc =
                viper::codegen::common::prepareLinkContextFromSymbols(extSymbols, ctx, out, err);
            rc != 0) {
            result.exit_code = 1;
            result.stdout_text = out.str();
            result.stderr_text = err.str();
            return result;
        }

        std::filesystem::path exe =
            opts_.output_obj_path.empty()
                ? std::filesystem::path(opts_.input_il_path).replace_extension("")
                : std::filesystem::path(opts_.output_obj_path);

        if (opts_.link_mode == LinkMode::System)
            err << "warning: --system-link is deprecated; using the native linker\n";

        const int lrc =
            linkObjToExe(objPath.string(), exe.string(), ctx, opts_.stack_size, opts_.extra_objects, out, err);

        if (!outputIsObj) {
            std::error_code ec;
            std::filesystem::remove(objPath, ec);
        }

        if (lrc != 0) {
            result.exit_code = 1;
            result.stdout_text = out.str();
            result.stderr_text = err.str();
            return result;
        }

        if (opts_.run_native) {
            const int rc = viper::codegen::common::runExecutable(exe.string(), out, err);
            result.exit_code = rc == -1 ? 1 : rc;
        }

        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    if (!opts_.emit_asm && !common::writeTextFile(asmPath, asmText, err)) {
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    if (!opts_.output_obj_path.empty() && !opts_.run_native) {
        const std::string &outPath = opts_.output_obj_path;
        if (std::filesystem::path(outPath).extension() == ".o") {
            const int arc = viper::codegen::common::invokeAssembler(
                systemAssemblerArgs(), asmPath, outPath, out, err);
            result.exit_code = arc == 0 ? 0 : 1;
            result.stdout_text = out.str();
            result.stderr_text = err.str();
            return result;
        }

        if (opts_.link_mode == LinkMode::System)
            err << "warning: --system-link is deprecated; using the native linker\n";
        const int lrc =
            linkToExe(asmPath, outPath, opts_.stack_size, opts_.extra_objects, out, err);
        if (lrc == 0 && !opts_.emit_asm) {
            std::error_code ec;
            std::filesystem::remove(asmPath, ec);
        }
        result.exit_code = lrc == 0 ? 0 : 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    std::filesystem::path exe =
        opts_.output_obj_path.empty()
            ? std::filesystem::path(opts_.input_il_path).replace_extension("")
            : std::filesystem::path(opts_.output_obj_path);

    if (opts_.link_mode == LinkMode::System)
        err << "warning: --system-link is deprecated; using the native linker\n";
    if (linkToExe(asmPath, exe.string(), opts_.stack_size, opts_.extra_objects, out, err) != 0) {
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    if (!opts_.emit_asm) {
        std::error_code ec;
        std::filesystem::remove(asmPath, ec);
    }

    if (opts_.run_native) {
        const int rc = viper::codegen::common::runExecutable(exe.string(), out, err);
        result.exit_code = rc == -1 ? 1 : rc;
    }

    result.stdout_text = out.str();
    result.stderr_text = err.str();
    return result;
}

} // namespace viper::codegen::aarch64
