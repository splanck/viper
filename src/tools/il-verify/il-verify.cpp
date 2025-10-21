//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the standalone `il-verify` CLI. The executable accepts a textual IL
// module, parses it into an in-memory representation, runs the structural
// verifier, and reports the result to stdout/stderr. The tool is intentionally
// tiny so it doubles as a sample for embedding the parser and verifier in other
// utilities. All resources—including the module instance and source manager—are
// owned locally for the duration of the process.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the canonical IL verifier command-line tool.
/// @details Designed as both a developer utility and sample code, the binary
///          wires together the parser, verifier, and diagnostic reporting in the
///          minimal amount of code necessary to expose a user-facing CLI.

#include "support/source_manager.hpp"
#include "tools/il-verify/driver.hpp"
#include <iostream>
#include <string>

/// @brief Entry point for the `il-verify` binary.
///
/// @details The control flow performs the full verification pipeline:
///          1. Handle the `--version` flag by printing the IL version banner.
///          2. Validate the argument count and emit a usage message on mismatch.
///          3. Parse the IL file into a module using
///             @ref il::tools::common::loadModuleFromFile.
///          4. Run the verifier via @ref il::tools::common::verifyModule.
///          5. Print "OK" when verification succeeds or propagate the
///             appropriate error status when it fails.
///          The module and source manager are local stack objects so no
///          persistent state is retained between invocations.
///
/// @param argc Number of arguments supplied by the C runtime.
/// @param argv Argument vector pointing at UTF-8 encoded strings.
/// @return Zero on success or when printing the version banner; otherwise one
///         to signal argument, I/O, parse, or verification failures.
int main(int argc, char **argv)
{
    if (argc == 2 && std::string(argv[1]) == "--version")
    {
        std::cout << "IL v0.1.2\n";
        return 0;
    }
    if (argc != 2)
    {
        std::cerr << "Usage: il-verify <file.il>\n";
        return 1;
    }
    il::support::SourceManager sm;
    if (!il::tools::verify::runVerificationPipeline(argv[1], std::cout, std::cerr, sm))
    {
        return 1;
    }
    return 0;
}
