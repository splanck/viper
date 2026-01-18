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
#include "frontends/basic/Intrinsics.hpp"
#include "il/core/Module.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"
#include "viper/version.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <cstdlib>
#endif

namespace
{

/// @brief Print the viper version banner and runtime configuration summary.
///
/// @details The banner includes the viper version, current IL version, and whether
///          deterministic numerics are enabled. The routine is factored out so
///          both `main` and future subcommands can reuse it when handling
///          `--version` flags.
void printVersion()
{
    // Version banner
    std::cout << "viper v" << VIPER_VERSION_STR << "\n";
    if (std::string(VIPER_SNAPSHOT_STR).size())
        std::cout << "snap: " << VIPER_SNAPSHOT_STR << "\n";
    std::cout << "IL current: " << VIPER_IL_VERSION_STR << "\n";
    std::cout << "IL supported: 0.1.0 – " << VIPER_IL_VERSION_STR << "\n";
    std::cout << "Precise Numerics: enabled\n";
}

} // namespace

namespace
{
// --dump-runtime-descriptors implementation (stable formatting)
int dumpRuntimeDescriptors()
{
    using il::runtime::findRuntimeSignatureId;
    using il::runtime::RuntimeDescriptor;
    using il::runtime::runtimeRegistry;

    const auto &reg = runtimeRegistry();

    struct Key
    {
        std::optional<il::runtime::RtSig> sig{};
        il::runtime::RuntimeHandler handler{nullptr};

        bool operator==(const Key &o) const noexcept
        {
            return sig == o.sig && handler == o.handler;
        }
    };

    struct KeyHash
    {
        std::size_t operator()(const Key &k) const noexcept
        {
            const std::size_t h1 = k.sig ? static_cast<std::size_t>(*k.sig) : 0x9E3779B97F4A7C15ull;
            const std::size_t h2 = reinterpret_cast<std::size_t>(k.handler);
            return h1 ^ (h2 + 0x9E3779B97F4A7C15ull + (h1 << 6) + (h1 >> 2));
        }
    };

    std::unordered_map<Key, std::vector<const RuntimeDescriptor *>, KeyHash> groups;
    groups.reserve(reg.size());

    for (const auto &d : reg)
    {
        Key k;
        k.sig = findRuntimeSignatureId(d.name);
        k.handler = d.handler;
        groups[k].push_back(&d);
    }

    auto typeListToString = [](const std::vector<il::core::Type> &ts) -> std::string
    {
        std::ostringstream os;
        for (size_t i = 0; i < ts.size(); ++i)
        {
            if (i)
                os << ',';
            os << ts[i].toString();
        }
        return os.str();
    };

    auto effectsToString = [](const il::runtime::RuntimeSignature &s,
                              il::runtime::RuntimeTrapClass trap) -> std::string
    {
        std::vector<std::string> items;
        items.push_back(s.nothrow ? "NoThrow" : "MayThrow");
        if (s.readonly)
            items.push_back("ReadOnly");
        if (s.pure)
            items.push_back("Pure");
        if (trap != il::runtime::RuntimeTrapClass::None)
        {
            switch (trap)
            {
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
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (i)
                os << ", ";
            os << items[i];
        }
        return os.str();
    };

    // Stable output: iterate groups in registry order by the first appearance index
    // Build an index map for first descriptor pointer order
    std::unordered_map<const RuntimeDescriptor *, size_t> order;
    order.reserve(reg.size());
    for (size_t i = 0; i < reg.size(); ++i)
        order[&reg[i]] = i;

    std::vector<std::pair<size_t, std::vector<const RuntimeDescriptor *>>> orderedGroups;
    orderedGroups.reserve(groups.size());
    for (auto &kv : groups)
    {
        // sort entries in group by registry order for deterministic alias listing
        auto &vec = kv.second;
        std::sort(vec.begin(), vec.end(), [&](auto *a, auto *b) { return order[a] < order[b]; });
        size_t firstIdx = order[vec.front()];
        orderedGroups.emplace_back(firstIdx, vec);
    }
    std::sort(orderedGroups.begin(),
              orderedGroups.end(),
              [](auto &a, auto &b) { return a.first < b.first; });

    for (const auto &entry : orderedGroups)
    {
        const auto &vec = entry.second;
        // choose canonical: prefer Viper.* else first
        const RuntimeDescriptor *canonical = vec.front();
        for (const auto *d : vec)
        {
            if (d->name.rfind("Viper.", 0) == 0)
            {
                canonical = d;
                break;
            }
        }

        // collect aliases (rt_* only)
        std::vector<std::string_view> aliases;
        for (const auto *d : vec)
        {
            if (d == canonical)
                continue;
            if (d->name.rfind("rt_", 0) == 0)
                aliases.push_back(d->name);
        }

        // Print block
        std::cout << "NAME: " << canonical->name << "\n";

        std::cout << "  ALIASES: ";
        if (aliases.empty())
        {
            std::cout << "(none)\n";
        }
        else
        {
            for (size_t i = 0; i < aliases.size(); ++i)
            {
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

namespace
{
// --dump-runtime-classes implementation (stable formatting)
int dumpRuntimeClasses()
{
    const auto &classes = il::runtime::runtimeClassCatalog();
    for (const auto &c : classes)
    {
        std::cout << "CLASS " << (c.qname ? c.qname : "<unnamed>")
                  << " (type: " << (c.layout ? c.layout : "<unknown>") << ")\n";
        for (const auto &p : c.properties)
        {
            std::cout << "  PROP " << (p.name ? p.name : "<unnamed>") << ": "
                      << (p.type ? p.type : "<type>") << "  \u2192 "
                      << (p.getter ? p.getter : "<getter>") << "\n";
        }
        for (const auto &m : c.methods)
        {
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
/// @details Step-by-step summary:
///          1. Emit the tool banner with version information.
///          2. Print usage lines for the `-run`, `front basic`, and `il-opt`
///             subcommands, mirroring the behaviour of their handlers.
///          3. Provide IL and BASIC specific notes, including intrinsic listings
///             supplied by the BASIC front end.
void usage()
{
    std::cerr
        << "viper v" << VIPER_VERSION_STR << "\n"
        << "Usage: viper -run <file.il> [--trace=il|src] [--stdin-from <file>] [--max-steps N]"
           " [--break label|file:line]* [--break-src file:line]* [--watch name]* [--bounds-checks] "
           "[--count] [--time] [--dump-trap]\n"
        << "       viper front basic -emit-il <file.bas> [--bounds-checks] "
           "[--no-runtime-namespaces]\n"
        << "       viper front basic -run <file.bas> [--trace=il|src] [--stdin-from <file>] "
           "[--max-steps N] [--bounds-checks] [--dump-trap] [--no-runtime-namespaces]\n"
        << "       viper front pascal -emit-il <file.pas> [unit1.pas unit2.pas ...]\n"
        << "       viper front pascal -run <file.pas> [unit1.pas ...] [--trace=il|src] [--stdin-from "
           "<file>]\n"
        << "       viper codegen x64 -S <in.il> [-o <exe>] [--run-native]\n"
        << "       viper codegen arm64 <in.il> [-S <out.s>] [-o <exe|obj>] [-run-native]\n"
        << "       viper il-opt <in.il> -o <out.il> [--passes p1,p2] [-print-before] [-print-after]"
           " [-verify-each]\n"
        << "       viper bench <file.il> [file2.il ...] [-n N] [--table|--switch|--threaded] "
           "[--json]\n"
        << "\nIL notes:\n"
        << "  IL modules executed with -run must define func @main().\n"
        << "\nBASIC notes:\n"
        << "  FUNCTION must RETURN a value on all paths.\n"
        << "  SUB cannot be used as an expression.\n"
        << "  Array parameters are ByRef; pass the array variable, not an index.\n"
        << "  Runtime namespaces: default ON; pass --no-runtime-namespaces to disable.\n"
        << "  Intrinsics: ";
    il::frontends::basic::intrinsics::dumpNames(std::cerr);
    std::cerr << "\n";
}

namespace viper::tools::ilc
{

/// @brief Adapter invoked by `viper codegen x64` from the top-level driver.
/// @details The driver hands control to this helper with `argv` still pointing
///          at the architecture token. The helper strips it before delegating to
///          the actual command implementation so existing parsing logic
///          continues to expect the IL input path as the leading argument.
/// @param argc Number of command-line arguments following the "codegen"
///             subcommand.
/// @param argv Argument vector beginning with the target architecture token.
/// @return Exit status propagated from @ref cmd_codegen_x64.
int run_codegen_x64(int argc, char **argv)
{
    return cmd_codegen_x64(argc - 1, argv + 1);
}

} // namespace viper::tools::ilc

/// @brief Program entry for the `viper` command-line tool.
///
/// @param argc Number of command-line arguments.
/// @param argv Array of argument strings.
/// @return Exit status of the selected subcommand or `1` on error.
/// @details The first argument determines which handler processes the request:
///          `cmdRunIL` executes `.il` programs, `cmdILOpt` performs optimization
///          passes, and `cmdFrontBasic` drives the BASIC front end. Step-by-step
///          summary:
///          1. Verify that at least one subcommand argument is provided.
///          2. Handle `--version` by delegating to @ref printVersion.
///          3. Dispatch to the matching handler with the remaining arguments.
///          4. Fall back to displaying usage when no match exists.
int main(int argc, char **argv)
{
#ifdef _WIN32
    // Disable Windows abort dialog so runtime panics exit cleanly
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    if (argc < 2)
    {
        usage();
        return 1;
    }
    std::string cmd = argv[1];
    if (cmd == "--version")
    {
        printVersion();
        return 0;
    }
    if (cmd == "--dump-runtime-descriptors")
    {
        return dumpRuntimeDescriptors();
    }
    if (cmd == "--dump-runtime-classes")
    {
        return dumpRuntimeClasses();
    }
    if (cmd == "-run")
    {
        return cmdRunIL(argc - 2, argv + 2);
    }
    if (cmd == "il-opt")
    {
        return cmdILOpt(argc - 2, argv + 2);
    }
    if (cmd == "bench")
    {
        return cmdBench(argc - 2, argv + 2);
    }
    if (cmd == "codegen")
    {
        if (argc >= 3)
        {
            if (std::string_view(argv[2]) == "x64")
            {
                return viper::tools::ilc::run_codegen_x64(argc - 2, argv + 2);
            }
            if (std::string_view(argv[2]) == "arm64")
            {
                return viper::tools::ilc::cmd_codegen_arm64(argc - 3, argv + 3);
            }
        }
        usage();
        return 1;
    }
    if (cmd == "front" && argc >= 3 && std::string(argv[2]) == "basic")
    {
        return cmdFrontBasic(argc - 3, argv + 3);
    }
#ifdef VIPER_ENABLE_PASCAL
    if (cmd == "front" && argc >= 3 && std::string(argv[2]) == "pascal")
    {
        return cmdFrontPascal(argc - 3, argv + 3);
    }
#endif
    if (cmd == "front" && argc >= 3 && std::string(argv[2]) == "zia")
    {
        return cmdFrontZia(argc - 3, argv + 3);
    }
    usage();
    return 1;
}
