//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
// Links: tools/lsp-common/LspHandler.hpp, tools/lsp-common/McpHandler.hpp
//
//===----------------------------------------------------------------------===//

#include "tools/lsp-common/Json.hpp"
#include "tools/lsp-common/JsonRpc.hpp"
#include "tools/lsp-common/LspHandler.hpp"
#include "tools/lsp-common/McpHandler.hpp"
#include "tools/lsp-common/Transport.hpp"
#include "tools/zia-server/CompilerBridge.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>

using namespace viper::server;

static const ServerConfig kZiaConfig{
    "zia-server", // serverName
    "0.1.0",      // version
    "zia",        // sourceName
    "zia",        // toolPrefix
    ".zia",       // defaultExt
    "Zia",        // langLabel
};

/// @brief Print zia-server usage (protocol mode flags) to stderr.
static void printUsage() {
    std::fprintf(stderr,
                 "Usage: zia-server [--mcp | --lsp | --help | --version]\n"
                 "\n"
                 "  --mcp      Serve MCP protocol (newline-delimited JSON-RPC)\n"
                 "  --lsp      Serve LSP protocol (Content-Length framed JSON-RPC)\n"
                 "  --help     Show this help message\n"
                 "  --version  Show version\n"
                 "\n"
                 "Default: auto-detect protocol from first input byte.\n");
}

/// @brief Print the zia-server version banner to stdout.
static void printVersion() {
    std::fprintf(stdout, "zia-server 0.1.0\n");
}

/// @brief Server protocol mode selected from the command line.
enum class Mode {
    Mcp,        ///< Serve the MCP protocol (newline-delimited JSON-RPC).
    Lsp,        ///< Serve the LSP protocol (Content-Length framed JSON-RPC).
    AutoDetect, ///< Choose MCP or LSP from the first byte of input.
};

/// @brief Main event loop: read messages, dispatch, write responses.
static int runMcpServer(Transport &transport, CompilerBridge &bridge) {
    McpHandler handler(bridge, kZiaConfig);
    RawMessage msg;

    while (transport.readMessage(msg)) {
        JsonValue json;
        try {
            json = JsonValue::parse(msg.content);
        } catch (const std::exception &) {
            transport.writeMessage(buildError(JsonValue(), kParseError, "Parse error"));
            continue;
        }

        JsonRpcRequest req;
        if (!parseRequest(json, req)) {
            transport.writeMessage(buildError(JsonValue(), kInvalidRequest, "Invalid Request"));
            continue;
        }

        std::string response;
        try {
            response = handler.handleRequest(req);
        } catch (const std::exception &e) {
            response = buildError(req.id, kInternalError, e.what());
        }
        if (!response.empty())
            transport.writeMessage(response);
    }
    return 0;
}

/// @brief Main event loop for LSP protocol.
static int runLspServer(Transport &transport, CompilerBridge &bridge) {
    LspHandler handler(bridge, transport, kZiaConfig);
    RawMessage msg;
    const bool verbose = std::getenv("VIPER_LSP_LOG") != nullptr;

    if (verbose) {
        std::fprintf(stderr, "[zia-server] LSP server started\n");
        std::fflush(stderr);
    }

    while (transport.readMessage(msg)) {
        if (verbose) {
            std::fprintf(stderr, "[zia-server] recv %zu bytes\n", msg.content.size());
            std::fflush(stderr);
        }

        JsonValue json;
        try {
            json = JsonValue::parse(msg.content);
        } catch (const std::exception &e) {
            if (verbose) {
                std::fprintf(stderr, "[zia-server] parse error: %s\n", e.what());
                std::fflush(stderr);
            }
            transport.writeMessage(buildError(JsonValue(), kParseError, "Parse error"));
            continue;
        }

        JsonRpcRequest req;
        if (!parseRequest(json, req)) {
            if (verbose) {
                std::fprintf(stderr, "[zia-server] invalid request\n");
                std::fflush(stderr);
            }
            transport.writeMessage(buildError(JsonValue(), kInvalidRequest, "Invalid Request"));
            continue;
        }

        if (verbose) {
            std::fprintf(stderr,
                         "[zia-server] method=%s id=%s\n",
                         req.method.c_str(),
                         req.id.isNull() ? "null" : req.id.toCompactString().c_str());
            std::fflush(stderr);
        }

        // exit notification ends the loop
        if (req.method == "exit")
            return handler.shutdownRequested() ? 0 : 1;

        std::string response;
        try {
            response = handler.handleRequest(req);
        } catch (const std::exception &e) {
            if (verbose) {
                std::fprintf(stderr, "[zia-server] handler error: %s\n", e.what());
                std::fflush(stderr);
            }
            response = buildError(req.id, kInternalError, e.what());
        }
        if (!response.empty()) {
            if (verbose) {
                std::fprintf(stderr, "[zia-server] resp %zu bytes\n", response.size());
                std::fflush(stderr);
            }
            transport.writeMessage(response);
        } else if (verbose) {
            std::fprintf(stderr, "[zia-server] (no response — notification handled)\n");
            std::fflush(stderr);
        }
    }

    if (verbose) {
        std::fprintf(stderr, "[zia-server] LSP server exiting\n");
        std::fflush(stderr);
    }
    return 0;
}

/// @brief Entry point for the Zia language server.
/// @details Parses --mcp/--lsp/--help/--version, then runs the selected protocol
///          server (auto-detecting from the first input byte by default) with a
///          CompilerBridge backing the shared LSP/MCP handlers.
/// @param argc Argument count from the C runtime.
/// @param argv Argument vector from the C runtime.
/// @return Process exit status (0 on clean shutdown).
int main(int argc, char **argv) {
    platformInitStdio();

    Mode mode = Mode::AutoDetect;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mcp") == 0)
            mode = Mode::Mcp;
        else if (std::strcmp(argv[i], "--lsp") == 0)
            mode = Mode::Lsp;
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printUsage();
            return 0;
        } else if (std::strcmp(argv[i], "--version") == 0) {
            printVersion();
            return 0;
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", argv[i]);
            printUsage();
            return 1;
        }
    }

    // Auto-detect: peek at first byte
    if (mode == Mode::AutoDetect) {
        int c = std::fgetc(stdin);
        if (c == EOF)
            return 0;
        std::ungetc(c, stdin);

        if (c == 'C' || c == 'c')
            mode = Mode::Lsp; // Content-Length header
        else
            mode = Mode::Mcp; // JSON starts with {
    }

    CompilerBridge bridge;

    std::unique_ptr<Transport> transport;
    if (mode == Mode::Lsp) {
        transport = std::make_unique<LspTransport>(stdin, stdout);
        return runLspServer(*transport, bridge);
    } else {
        transport = std::make_unique<McpTransport>(stdin, stdout);
        return runMcpServer(*transport, bridge);
    }
}
