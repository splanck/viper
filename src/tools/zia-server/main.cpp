//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/zia-server/main.cpp
// Purpose: Entry point for the Zia language server (MCP + LSP).
// Key invariants:
//   - --mcp: newline-delimited JSON-RPC (MCP protocol)
//   - --lsp: Content-Length framed JSON-RPC (LSP protocol)
//   - Default: auto-detect from first byte of input
// Ownership/Lifetime:
//   - Process lifetime; single-threaded event loop
// Links: tools/lsp-common/ServerMain.hpp, tools/zia-server/CompilerBridge.hpp
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Defines the Zia language-server entry point.
/// @details All protocol handling is shared with the BASIC language server via
///          ServerMain.hpp; this file supplies Zia-specific metadata and bridge
///          construction.

#include "common/Utf8CommandLine.hpp"
#include "tools/lsp-common/ServerMain.hpp"
#include "tools/zia-server/CompilerBridge.hpp"
#include "zanna/version.hpp"

using namespace zanna::server;

/// @brief Zia-specific labels and protocol metadata.
static const ServerConfig kZiaConfig{
    "zia-server",      // serverName
    ZANNA_VERSION_STR, // version
    "zia",             // sourceName
    "zia",             // toolPrefix
    ".zia",            // defaultExt
    "Zia",             // langLabel
};

/// @brief Entry point for the Zia language server.
/// @details Delegates common argument parsing, protocol auto-detection, and
///          MCP/LSP event loops to @ref runLanguageServerMain.
/// @param argc Argument count from the C runtime.
/// @param argv Argument vector from the C runtime.
/// @return Process exit status.
int main(int argc, char **argv) {
    zanna::tools::Utf8CommandLine commandLine(argc, argv);
    if (!commandLine.applyOrReport(argc, argv))
        return 1;
    return runLanguageServerMain<CompilerBridge>(argc, argv, "zia-server", kZiaConfig);
}
