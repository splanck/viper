//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the top-level `viper` driver. The executable dispatches to
// subcommands that run IL programs, compile BASIC, or apply optimizer passes.
// Shared CLI plumbing lives in cli.cpp; this file wires those helpers into the
// `main` entry point and prints user-facing usage information.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Entry point and usage utilities for the `viper` driver.
/// @details The translation unit owns only user-interface glue; heavy lifting
///          such as pass management or VM execution is delegated to subcommands.

#include "cli.hpp"
#include "cmd_codegen_arm64.hpp"
#include "cmd_codegen_x64.hpp"
#include "il/core/Module.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"
#include "viper/version.hpp"
#include <algorithm>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <cstdlib>
#endif

namespace {

/// @brief Print the viper version banner and runtime configuration summary.
///
/// @details The banner includes the viper version, current IL version, and whether
///          deterministic numerics are enabled. The routine is factored out so
///          both `main` and future subcommands can reuse it when handling
///          `--version` flags.
void printVersion() {
    // Version banner
    std::cout << "viper v" << VIPER_VERSION_STR << "\n";
    if (std::string(VIPER_SNAPSHOT_STR).size())
        std::cout << "snap: " << VIPER_SNAPSHOT_STR << "\n";
    std::cout << "IL current: " << VIPER_IL_VERSION_STR << "\n";
    std::cout << "IL supported: 0.1.0 – " << VIPER_IL_VERSION_STR << "\n";
    std::cout << "Precise Numerics: enabled\n";
}

} // namespace

namespace {
/// @brief Implement `--dump-runtime-descriptors`: print the runtime descriptor
///        table in a stable, sorted format to stdout.
/// @return 0 on success.
int dumpRuntimeDescriptors() {
    using il::runtime::findRuntimeSignatureId;
    using il::runtime::RuntimeDescriptor;
    using il::runtime::runtimeRegistry;

    const auto &reg = runtimeRegistry();

    struct DescriptorGroup {
        std::optional<il::runtime::RtSig> sig{};
        il::runtime::RuntimeHandler handler{nullptr};
        std::vector<const RuntimeDescriptor *> descriptors;
        size_t firstIndex{0};

        bool matches(std::optional<il::runtime::RtSig> otherSig,
                     il::runtime::RuntimeHandler otherHandler) const noexcept {
            return sig == otherSig && handler == otherHandler;
        }
    };

    std::vector<DescriptorGroup> groups;
    groups.reserve(reg.size());

    for (size_t i = 0; i < reg.size(); ++i) {
        const auto &d = reg[i];
        const auto sig = findRuntimeSignatureId(d.name);
        auto it = std::find_if(groups.begin(), groups.end(), [&](const DescriptorGroup &group) {
            return group.matches(sig, d.handler);
        });
        if (it == groups.end()) {
            DescriptorGroup group;
            group.sig = sig;
            group.handler = d.handler;
            group.firstIndex = i;
            group.descriptors.push_back(&d);
            groups.push_back(std::move(group));
        } else {
            it->descriptors.push_back(&d);
        }
    }

    auto typeListToString = [](const std::vector<il::core::Type> &ts) -> std::string {
        std::ostringstream os;
        for (size_t i = 0; i < ts.size(); ++i) {
            if (i)
                os << ',';
            os << ts[i].toString();
        }
        return os.str();
    };

    auto effectsToString = [](const il::runtime::RuntimeSignature &s,
                              il::runtime::RuntimeTrapClass trap) -> std::string {
        std::vector<std::string> items;
        items.push_back(s.nothrow ? "NoThrow" : "MayThrow");
        if (s.readonly)
            items.push_back("ReadOnly");
        if (s.pure)
            items.push_back("Pure");
        if (trap != il::runtime::RuntimeTrapClass::None) {
            switch (trap) {
                case il::runtime::RuntimeTrapClass::PowDomainOverflow:
                    items.emplace_back("Trap:PowDomainOverflow");
                    break;
                default:
                    items.emplace_back("Trap:Unknown");
                    break;
            }
        }
        if (items.empty())
            return std::string("None");
        std::ostringstream os;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i)
                os << ", ";
            os << items[i];
        }
        return os.str();
    };

    std::sort(groups.begin(), groups.end(), [](const auto &a, const auto &b) {
        return a.firstIndex < b.firstIndex;
    });

    for (const auto &entry : groups) {
        const auto &vec = entry.descriptors;
        // choose canonical: prefer Viper.* else first
        const RuntimeDescriptor *canonical = vec.front();
        for (const auto *d : vec) {
            if (d->name.rfind("Viper.", 0) == 0) {
                canonical = d;
                break;
            }
        }

        // collect aliases (rt_* only)
        std::vector<std::string_view> aliases;
        for (const auto *d : vec) {
            if (d == canonical)
                continue;
            if (d->name.rfind("rt_", 0) == 0)
                aliases.push_back(d->name);
        }

        // Print block
        std::cout << "NAME: " << canonical->name << "\n";

        std::cout << "  ALIASES: ";
        if (aliases.empty()) {
            std::cout << "(none)\n";
        } else {
            for (size_t i = 0; i < aliases.size(); ++i) {
                if (i)
                    std::cout << ", ";
                std::cout << aliases[i];
            }
            std::cout << "\n";
        }

        const auto &sig = canonical->signature;
        std::cout << "  SIGNATURE: " << sig.retType.toString() << '('
                  << typeListToString(sig.paramTypes) << ")\n";
        std::cout << "  EFFECTS: " << effectsToString(sig, canonical->trapClass) << "\n";
    }
    return 0;
}
} // namespace

namespace {
/// @brief Implement `--dump-runtime-classes`: print the runtime class catalog
///        (properties, methods, constructors) in a stable format to stdout.
/// @return 0 on success.
int dumpRuntimeClasses() {
    const auto &classes = il::runtime::runtimeClassCatalog();
    for (const auto &c : classes) {
        std::cout << "CLASS " << (c.qname ? c.qname : "<unnamed>")
                  << " (type: " << (c.layout ? c.layout : "<unknown>") << ")\n";
        for (const auto &p : c.properties) {
            std::cout << "  PROP " << (p.name ? p.name : "<unnamed>") << ": "
                      << (p.type ? p.type : "<type>") << "  \u2192 "
                      << (p.getter ? p.getter : "<getter>") << "\n";
        }
        for (const auto &m : c.methods) {
            std::cout << "  METH " << (m.name ? m.name : "<unnamed>") << "("
                      << (m.signature ? m.signature : "") << ") \u2192 "
                      << (m.target ? m.target : "<target>") << "\n";
        }
        if (c.ctor && std::string(c.ctor).size())
            std::cout << "  CTOR \u2192 " << c.ctor << "\n";
    }
    return 0;
}
} // namespace

/// @brief Print synopsis and option hints for the `viper` CLI.
///
/// @details The top-level help intentionally stays short. Subcommand-specific
///          help carries detailed flags so unrelated implementation options do
///          not crowd the common command list.
void usage() {
    std::cerr
        << "viper v" << VIPER_VERSION_STR << "\n"
        << "Usage: viper <command> [arguments]\n"
        << "\n"
        << "Common commands:\n"
        << "       viper run [target] [options] [-- program-args...]\n"
        << "       viper build [target] [-o output] [options]\n"
        << "       viper init <project-name> [--lang zia|basic]\n"
        << "       viper repl [zia|basic]\n"
        << "       viper package [target] [--target macos|linux|windows|tarball] [-o output]\n"
        << "\n"
        << "Developer commands:\n"
        << "       viper front zia|basic ...\n"
        << "       viper -run <file.il> ...\n"
        << "       viper il-opt <file.il> -o <out.il> ...\n"
        << "       viper codegen x64|arm64 <file.il> ...\n"
        << "       viper bench <file.il> ...\n"
        << "       viper install-package ...\n"
        << "\n"
        << "Targets:\n"
        << "  A target is a .zia file, .bas file, directory, or viper.project path.\n"
        << "  If omitted where supported, the target defaults to the current directory.\n"
        << "\n"
        << "Help:\n"
        << "       viper help <command>\n"
        << "       viper help package\n"
        << "       viper help front zia|basic\n"
        << "       viper help codegen x64|arm64\n"
        << "       viper --version\n";
}

/// @brief Invoke a subcommand handler with a synthetic `--help` argument vector.
/// @details Lets `viper help <command>` reuse each subcommand's own help text.
int invokeHelp(int (*handler)(int, char **)) {
    char helpFlag[] = "--help";
    char *helpArgv[] = {helpFlag};
    return handler(1, helpArgv);
}

/// @brief Print usage for the `viper codegen` subcommand (architectures + options).
void codegenUsage() {
    std::cerr << "Usage: viper codegen <arch> <file.il> [options]\n"
              << "\n"
              << "Architectures:\n"
              << "  x64\n"
              << "  arm64\n"
              << "\n"
              << "Use 'viper help codegen x64' or 'viper help codegen arm64' for backend options.\n";
}

namespace viper::tools::ilc {

/// @brief Adapter invoked by `viper codegen x64` from the top-level driver.
/// @details The driver hands control to this helper with `argv` still pointing
///          at the architecture token. The helper strips it before delegating to
///          the actual command implementation so existing parsing logic
///          continues to expect the IL input path as the leading argument.
/// @param argc Number of command-line arguments following the "codegen"
///             subcommand.
/// @param argv Argument vector beginning with the target architecture token.
/// @return Exit status propagated from @ref cmd_codegen_x64.
int run_codegen_x64(int argc, char **argv) {
    return cmd_codegen_x64(argc - 1, argv + 1);
}

} // namespace viper::tools::ilc

/// @brief Program entry for the `viper` command-line tool.
///
/// @param argc Number of command-line arguments.
/// @param argv Array of argument strings.
/// @return Exit status of the selected subcommand or `1` on error.
/// @details The first argument determines which handler processes the request:
///          `cmdRunIL` executes `.il` programs, `cmdILOpt` performs
///          optimization passes, and `cmdFrontZia` / `cmdFrontBasic` drive the
///          legacy frontend entry points. Step-by-step summary:
///          1. Verify that at least one subcommand argument is provided.
///          2. Handle `--version` by delegating to @ref printVersion.
///          3. Dispatch to the matching handler with the remaining arguments.
///          4. Fall back to displaying usage when no match exists.
int main(int argc, char **argv) {
#ifdef _WIN32
    // Disable Windows abort dialog so runtime panics exit cleanly
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    if (argc < 2) {
        usage();
        return 1;
    }
    std::string cmd = argv[1];
    if (cmd == "--help" || cmd == "-h") {
        usage();
        return 0;
    }
    if (cmd == "--version") {
        printVersion();
        return 0;
    }
    if (cmd == "--dump-runtime-descriptors") {
        return dumpRuntimeDescriptors();
    }
    if (cmd == "--dump-runtime-classes") {
        return dumpRuntimeClasses();
    }
    if (cmd == "run") {
        return cmdRun(argc - 2, argv + 2);
    }
    if (cmd == "build") {
        return cmdBuild(argc - 2, argv + 2);
    }
    if (cmd == "init") {
        return cmdInit(argc - 2, argv + 2);
    }
    if (cmd == "package") {
        return cmdPackage(argc - 2, argv + 2);
    }
    if (cmd == "install-package") {
        return cmdInstallPackage(argc - 2, argv + 2);
    }
    if (cmd == "repl") {
        return cmdRepl(argc - 2, argv + 2);
    }
    if (cmd == "help") {
        if (argc < 3) {
            usage();
            return 1;
        }
        const std::string_view topic = argv[2];
        if (topic == "run")
            return invokeHelp(cmdRun);
        if (topic == "build")
            return invokeHelp(cmdBuild);
        if (topic == "init")
            return invokeHelp(cmdInit);
        if (topic == "package")
            return invokeHelp(cmdPackage);
        if (topic == "install-package")
            return invokeHelp(cmdInstallPackage);
        if (topic == "repl")
            return invokeHelp(cmdRepl);
        if (topic == "-run")
            return invokeHelp(cmdRunIL);
        if (topic == "il-opt")
            return invokeHelp(cmdILOpt);
        if (topic == "bench")
            return invokeHelp(cmdBench);
        if (topic == "front") {
            if (argc >= 4 && std::string_view(argv[3]) == "zia")
                return invokeHelp(cmdFrontZia);
            if (argc >= 4 && std::string_view(argv[3]) == "basic")
                return invokeHelp(cmdFrontBasic);
            std::cerr << "Usage: viper front zia|basic ...\n"
                      << "Use 'viper help front zia' or 'viper help front basic'.\n";
            return 1;
        }
        if (topic == "codegen") {
            if (argc >= 4 && std::string_view(argv[3]) == "x64")
                return invokeHelp(viper::tools::ilc::cmd_codegen_x64);
            if (argc >= 4 && std::string_view(argv[3]) == "arm64")
                return invokeHelp(viper::tools::ilc::cmd_codegen_arm64);
            codegenUsage();
            return 1;
        }
        std::cerr << "unknown help topic: " << topic << "\n";
        usage();
        return 1;
    }
    if (cmd == "-run") {
        return cmdRunIL(argc - 2, argv + 2);
    }
    if (cmd == "il-opt") {
        return cmdILOpt(argc - 2, argv + 2);
    }
    if (cmd == "bench") {
        return cmdBench(argc - 2, argv + 2);
    }
    if (cmd == "codegen") {
        if (argc >= 3 && (std::string_view(argv[2]) == "--help" ||
                          std::string_view(argv[2]) == "-h")) {
            codegenUsage();
            return 0;
        }
        if (argc >= 3) {
            if (std::string_view(argv[2]) == "x64") {
                return viper::tools::ilc::run_codegen_x64(argc - 2, argv + 2);
            }
            if (std::string_view(argv[2]) == "arm64") {
                return viper::tools::ilc::cmd_codegen_arm64(argc - 3, argv + 3);
            }
        }
        codegenUsage();
        return 1;
    }
    if (cmd == "front" && argc >= 3 && std::string(argv[2]) == "basic") {
        return cmdFrontBasic(argc - 3, argv + 3);
    }
    if (cmd == "front" && argc >= 3 && std::string(argv[2]) == "zia") {
        return cmdFrontZia(argc - 3, argv + 3);
    }
    usage();
    return 1;
}
