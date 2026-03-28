//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/ilc/cmd_codegen_arm64.cpp
// Purpose: Thin CLI adapter around the reusable AArch64 code-generation pipeline.
//
//===----------------------------------------------------------------------===//

#include "cmd_codegen_arm64.hpp"

#include "codegen/aarch64/CodegenPipeline.hpp"
#include "tools/common/ArgvView.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace viper::tools::ilc {
namespace {

using viper::tools::ArgvView;

constexpr std::string_view kUsage =
    "usage: ilc codegen arm64 <file.il> [-S <file.s>] [-o <a.out>] [-run-native]\n"
    "       [--dump-mir-before-ra] [--dump-mir-after-ra] [--dump-mir-full]\n"
    "       [--native-asm|--system-asm] [--native-link|--system-link] [-O0|-O1|-O2]\n";

using Pipeline = viper::codegen::aarch64::CodegenPipeline;

struct ParseOutcome {
    std::optional<Pipeline::Options> opts{};
    std::string diagnostics{};
};

ParseOutcome parseArgs(const ArgvView &args) {
    ParseOutcome outcome{};
    if (args.empty()) {
        outcome.diagnostics = std::string{kUsage};
        return outcome;
    }

    Pipeline::Options opts{};
    opts.input_il_path = std::string(args.front());

    std::ostringstream diag;
    for (int i = 1; i < args.argc; ++i) {
        const std::string_view tok = args.at(i);
        if (tok == "-S") {
            if (i + 1 >= args.argc) {
                diag << "error: -S requires an output path\n" << kUsage;
                outcome.diagnostics = diag.str();
                return outcome;
            }
            opts.emit_asm = true;
            opts.output_asm_path = std::string(args.at(++i));
            continue;
        }
        if (tok == "-o") {
            if (i + 1 >= args.argc) {
                diag << "error: -o requires an output path\n" << kUsage;
                outcome.diagnostics = diag.str();
                return outcome;
            }
            opts.output_obj_path = std::string(args.at(++i));
            continue;
        }
        if (tok == "-run-native") {
            opts.run_native = true;
            continue;
        }
        if (tok == "--dump-mir-before-ra") {
            opts.dump_mir_before_ra = true;
            continue;
        }
        if (tok == "--dump-mir-after-ra") {
            opts.dump_mir_after_ra = true;
            continue;
        }
        if (tok == "--dump-mir-full") {
            opts.dump_mir_before_ra = true;
            opts.dump_mir_after_ra = true;
            continue;
        }
        if (tok == "-O" || tok == "--optimize") {
            if (i + 1 >= args.argc) {
                diag << "error: -O requires a level (0, 1, or 2)\n" << kUsage;
                outcome.diagnostics = diag.str();
                return outcome;
            }
            opts.optimize = std::atoi(std::string(args.at(++i)).c_str());
            continue;
        }
        if (tok.size() == 3 && tok[0] == '-' && tok[1] == 'O' && tok[2] >= '0' && tok[2] <= '2') {
            opts.optimize = tok[2] - '0';
            continue;
        }
        if (tok == "--native-asm") {
            opts.assembler_mode = Pipeline::AssemblerMode::Native;
            continue;
        }
        if (tok == "--system-asm") {
            opts.assembler_mode = Pipeline::AssemblerMode::System;
            continue;
        }
        if (tok == "--native-link") {
            opts.link_mode = Pipeline::LinkMode::Native;
            continue;
        }
        if (tok == "--system-link") {
            opts.link_mode = Pipeline::LinkMode::System;
            continue;
        }

        diag << "error: unknown flag '" << tok << "'\n" << kUsage;
        outcome.diagnostics = diag.str();
        return outcome;
    }

    outcome.opts = std::move(opts);
    return outcome;
}

} // namespace

int cmd_codegen_arm64(int argc, char **argv) {
    const ArgvView args{argc, argv};
    const ParseOutcome parsed = parseArgs(args);
    if (!parsed.opts.has_value()) {
        if (!parsed.diagnostics.empty())
            std::cerr << parsed.diagnostics;
        return 1;
    }

    try {
        Pipeline pipeline(*parsed.opts);
        const viper::codegen::aarch64::PipelineResult result = pipeline.run();
        if (!result.stdout_text.empty())
            std::cout << result.stdout_text;
        if (!result.stderr_text.empty())
            std::cerr << result.stderr_text;
        return result.exit_code;
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << '\n';
        return 2;
    }
}

} // namespace viper::tools::ilc
