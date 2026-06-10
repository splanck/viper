//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/common/frontend_tool.hpp
// Purpose: Shared infrastructure for language frontend CLI tools (vbasic, zia).
// Key invariants: All frontend tools share the same argument parsing logic.
// Ownership/Lifetime: Helpers are stateless aside from buildIlcArgs, whose
//                     returned char* vector aliases into a caller-owned storage
//                     vector that must outlive it.
// Links: docs/architecture.md, src/tools/common/native_compiler.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tools/common/native_compiler.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace viper::tools {

/// @brief Return a lowercased ASCII copy of @p value.
/// @details Frontend tool file-extension parsing should behave predictably across
///          case-insensitive and case-sensitive filesystems. Only ASCII is folded
///          because source extensions are ASCII command-line syntax.
inline std::string lowerAscii(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered;
}

/// @brief Test whether @p path ends with @p extension using ASCII case folding.
/// @details This keeps wrapper tools from rejecting valid source paths such as
///          `MAIN.ZIA` on platforms and editors that preserve uppercase suffixes.
inline bool endsWithExtensionInsensitive(std::string_view path, std::string_view extension) {
    if (path.size() < extension.size())
        return false;
    return lowerAscii(path.substr(path.size() - extension.size())) == lowerAscii(extension);
}

/// @brief Configuration parsed from frontend tool command-line arguments.
struct FrontendToolConfig {
    /// @brief Path to the source file to compile.
    std::string sourcePath;

    /// @brief Path for the output file (IL or native binary).
    std::string outputPath;

    /// @brief Whether to emit IL text instead of running the program.
    bool emitIl = false;

    /// @brief Whether to run the compiled program immediately.
    bool run = false;

    /// @brief Additional flags forwarded to the underlying ilc frontend.
    std::vector<std::string> forwardedArgs;

    /// @brief Arguments passed to the program at runtime (after "--" separator).
    std::vector<std::string> programArgs;

    /// @brief Optional architecture override for native code generation.
    std::optional<TargetArch> archOverride;
};

/// @brief Callbacks for language-specific behavior in frontend tools.
struct FrontendToolCallbacks {
    /// @brief File extension for this language (e.g., ".bas", ".zia").
    std::string_view fileExtension;

    /// @brief Language name for error messages (e.g., "BASIC", "Zia").
    std::string_view languageName;

    /// @brief Callback to print usage/help information for the tool.
    std::function<void()> printUsage;

    /// @brief Callback to print version information for the tool.
    std::function<void()> printVersion;

    /// @brief The ilc frontend command to invoke for compilation.
    std::function<int(int, char **)> frontendCommand;
};

/// @brief Parse frontend tool arguments into a @ref FrontendToolConfig.
///
/// @details Walks @p argv recognising the shared option vocabulary:
///          - `-h`/`--help` and `--version` invoke the matching callback and
///            terminate via @c std::exit(0).
///          - `-o`/`--output` records the output path and implies `--emit-il`.
///          - `--arch arm64|x64` overrides the native target architecture.
///          - `--` ends option parsing; everything after it becomes a program
///            argument forwarded to the running program.
///          - Any other leading-dash token is forwarded verbatim to the ilc
///            frontend, consuming a following value for the flags that take one
///            (`--stdin-from`, `--max-steps`, `--diagnostic-format`,
///            `--build-profile`).
///          - A token ending in @ref FrontendToolCallbacks::fileExtension is the
///            single source file; a second source file is an error.
///          Missing required values, unknown tokens, or a missing source file
///          print usage and terminate via @c std::exit(1). When no output mode is
///          requested the config defaults to running the program.
///
/// @param argc Number of command-line arguments.
/// @param argv Array of argument strings.
/// @param callbacks Language-specific callbacks (usage/version text, extension).
/// @return Parsed configuration; the function does not return on
///         error/help/version because it terminates the process.
inline FrontendToolConfig parseArgs(int argc, char **argv, const FrontendToolCallbacks &callbacks) {
    FrontendToolConfig config{};

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            callbacks.printUsage();
            std::exit(0);
        } else if (arg == "--version") {
            callbacks.printVersion();
            std::exit(0);
        } else if (arg == "--emit-il") {
            config.emitIl = true;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                std::cerr << "error: " << arg << " requires an output path\n\n";
                callbacks.printUsage();
                std::exit(1);
            }
            config.outputPath = argv[++i];
            config.emitIl = true; // -o implies emit-il
        } else if (arg == "--arch") {
            if (i + 1 >= argc) {
                std::cerr << "error: --arch requires arm64 or x64\n\n";
                callbacks.printUsage();
                std::exit(1);
            }
            std::string_view val = argv[++i];
            if (val == "arm64")
                config.archOverride = TargetArch::ARM64;
            else if (val == "x64")
                config.archOverride = TargetArch::X64;
            else {
                std::cerr << "error: --arch must be 'arm64' or 'x64'\n\n";
                callbacks.printUsage();
                std::exit(1);
            }
        } else if (arg == "--") {
            // Remaining arguments are program arguments
            for (int j = i + 1; j < argc; ++j)
                config.programArgs.emplace_back(argv[j]);
            break;
        } else if (arg.starts_with("-")) {
            // Forward other flags to underlying ilc implementation
            config.forwardedArgs.push_back(std::string(arg));

            // Check if this flag takes an argument
            if (arg == "--stdin-from" || arg == "--max-steps" || arg == "--diagnostic-format" ||
                arg == "--build-profile") {
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    config.forwardedArgs.push_back(argv[++i]);
                } else {
                    std::cerr << "error: " << arg << " requires an argument\n\n";
                    callbacks.printUsage();
                    std::exit(1);
                }
            }
        } else if (endsWithExtensionInsensitive(arg, callbacks.fileExtension)) {
            if (!config.sourcePath.empty()) {
                std::cerr << "error: multiple source files not supported\n\n";
                callbacks.printUsage();
                std::exit(1);
            }
            config.sourcePath = std::string(arg);
        } else {
            std::cerr << "error: unknown argument or file type: " << arg << "\n";
            std::cerr << "       (expected " << callbacks.fileExtension << " file)\n\n";
            callbacks.printUsage();
            std::exit(1);
        }
    }

    // Validate configuration
    if (config.sourcePath.empty()) {
        std::cerr << "error: no input file specified\n\n";
        callbacks.printUsage();
        std::exit(1);
    }

    // Default action: run the program
    if (!config.emitIl) {
        config.run = true;
    }

    return config;
}

/// @brief Build the argument vector for the ilc frontend subcommand.
///
/// @details Assembles the equivalent of an `argv` array for the underlying ilc
///          frontend: a leading mode flag (`-emit-il` or `-run`), the source
///          path, every forwarded flag, and the program arguments after a `--`
///          separator. The strings are first appended to @p outStorage so they
///          have a stable address, then a parallel vector of @c char* pointers
///          into that storage is returned.
///
/// @warning The returned vector aliases into @p outStorage. Callers must keep
///          @p outStorage alive (and unmodified) for as long as the returned
///          pointers are used; mutating @p outStorage afterwards may invalidate
///          them.
///
/// @param config Parsed frontend configuration.
/// @param outStorage Cleared and populated with the backing argument strings.
/// @return Argument vector of @c char* suitable for the frontend command.
inline std::vector<char *> buildIlcArgs(const FrontendToolConfig &config,
                                        std::vector<std::string> &outStorage) {
    outStorage.clear();
    outStorage.reserve(2 + config.forwardedArgs.size());

    std::vector<char *> args;

    // Add mode flag
    if (config.emitIl) {
        outStorage.push_back("-emit-il");
    } else {
        outStorage.push_back("-run");
    }

    // Add source path
    outStorage.push_back(config.sourcePath);

    // Add forwarded arguments
    for (const auto &fwd : config.forwardedArgs) {
        outStorage.push_back(fwd);
    }

    // Forward program arguments after '--' separator
    if (!config.programArgs.empty()) {
        outStorage.push_back("--");
        for (const auto &parg : config.programArgs) {
            outStorage.push_back(parg);
        }
    }

    // Build char* array from stable strings
    for (auto &str : outStorage) {
        args.push_back(str.data());
    }

    return args;
}

/// @brief Run a frontend tool end to end with the given callbacks.
///
/// @details Drives the full compile/run pipeline:
///          1. Parse arguments via @ref parseArgs.
///          2. Detect whether `-o` requests a native binary (a non-`.il` output
///             extension); if so, redirect IL emission to a temporary file so the
///             native code generator can consume it afterwards.
///          3. When an output path is set, redirect @c stdout to that file with
///             @c freopen (saving and later restoring the original descriptor so
///             native codegen can still write to the terminal).
///          4. Delegate to @ref FrontendToolCallbacks::frontendCommand to emit IL
///             or run the program.
///          5. On success with native output, invoke @ref compileToNative using
///             the requested or host-detected architecture.
///          6. Remove the temporary IL file, if any, before returning.
///
/// @param argc Number of command-line arguments.
/// @param argv Array of argument strings.
/// @param callbacks Language-specific callbacks.
/// @return Exit status: 0 on success, non-zero on error.
inline int runFrontendTool(int argc, char **argv, const FrontendToolCallbacks &callbacks) {
    if (argc < 2) {
        callbacks.printUsage();
        return 1;
    }

    // Parse arguments
    FrontendToolConfig config = parseArgs(argc, argv, callbacks);

    // Detect native output: -o with non-.il extension
    const bool nativeOutput = !config.outputPath.empty() && isNativeOutputPath(config.outputPath);

    std::string realOutputPath = config.outputPath;
    std::string tempIlPath;

    if (nativeOutput) {
        // Redirect IL to temp file; we'll codegen from it later
        tempIlPath = generateTempIlPath();
        config.outputPath = tempIlPath;
    }

    // Build argument vector for ilc frontend
    std::vector<std::string> argStorage;
    std::vector<char *> ilcArgs = buildIlcArgs(config, argStorage);

    // Handle -o output redirection (either to real file or temp file for native)
    FILE *outputFile = nullptr;
    int savedStdoutFd = -1;
    if (!config.outputPath.empty()) {
        // Save stdout so we can restore it after IL emission (needed for native codegen output)
#ifdef _WIN32
        savedStdoutFd = _dup(_fileno(stdout));
#else
        savedStdoutFd = dup(fileno(stdout));
#endif
        const std::filesystem::path outputParent =
            std::filesystem::path(config.outputPath).parent_path();
        if (!outputParent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(outputParent, ec);
            if (ec) {
                std::cerr << "error: failed to create output directory: " << outputParent.string()
                          << ": " << ec.message() << "\n";
                if (savedStdoutFd >= 0) {
#ifdef _WIN32
                    _close(savedStdoutFd);
#else
                    close(savedStdoutFd);
#endif
                }
                return 1;
            }
        }
        outputFile = std::freopen(config.outputPath.c_str(), "w", stdout);
        if (!outputFile) {
            std::cerr << "error: failed to open output file: " << config.outputPath << "\n";
            if (savedStdoutFd >= 0) {
#ifdef _WIN32
                _close(savedStdoutFd);
#else
                close(savedStdoutFd);
#endif
            }
            return 1;
        }
    }

    // Delegate to frontend implementation
    int result = callbacks.frontendCommand(static_cast<int>(ilcArgs.size()), ilcArgs.data());

    // Restore stdout if we redirected it
    if (outputFile) {
        if (std::fflush(stdout) != 0 && result == 0) {
            std::cerr << "error: failed to flush output file: " << config.outputPath << "\n";
            result = 1;
        }
        if (savedStdoutFd >= 0) {
#ifdef _WIN32
            if (_dup2(savedStdoutFd, _fileno(stdout)) < 0 && result == 0) {
                std::cerr << "error: failed to restore stdout\n";
                result = 1;
            }
            _close(savedStdoutFd);
#else
            if (dup2(savedStdoutFd, fileno(stdout)) < 0 && result == 0) {
                std::cerr << "error: failed to restore stdout\n";
                result = 1;
            }
            close(savedStdoutFd);
#endif
        } else {
            if (std::fclose(outputFile) != 0 && result == 0) {
                std::cerr << "error: failed to close output file: " << config.outputPath << "\n";
                result = 1;
            }
        }
    }

    // Native compilation step: compile the IL temp file to a binary
    if (result == 0 && nativeOutput) {
        auto arch = config.archOverride.value_or(detectHostArch());
        result = compileToNative(tempIlPath, realOutputPath, arch);
    }

    // Clean up temp file
    if (!tempIlPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(tempIlPath, ec);
    }

    return result;
}

} // namespace viper::tools
