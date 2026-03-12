//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/vbasic-server/main.cpp
// Purpose: Entry point for the Viper BASIC language server (MCP + LSP).
// Key invariants:
//   - --mcp: newline-delimited JSON-RPC (MCP protocol)
//   - --lsp: Content-Length framed JSON-RPC (LSP protocol)
//   - Default: auto-detect from first byte of input
// Ownership/Lifetime:
//   - Process lifetime; single-threaded event loop
// Links: tools/lsp-common/LspHandler.hpp, tools/lsp-common/McpHandler.hpp
//
//===----------------------------------------------------------------------===//

#include "tools/vbasic-server/BasicCompilerBridge.hpp"
#include "tools/lsp-common/Json.hpp"
#include "tools/lsp-common/JsonRpc.hpp"
#include "tools/lsp-common/LspHandler.hpp"
#include "tools/lsp-common/McpHandler.hpp"
#include "tools/lsp-common/Transport.hpp"

#include <cstdio>
#include <cstring>
#include <memory>

using namespace viper::server;

static const ServerConfig kBasicConfig{
    "vbasic-server",  // serverName
    "0.1.0",          // version
    "vbasic",         // sourceName
    "basic",          // toolPrefix
    ".bas",           // defaultExt
    "Viper BASIC",    // langLabel
};

static void printUsage()
{
    std::fprintf(stderr,
                 "Usage: vbasic-server [--mcp | --lsp | --help | --version]\n"
                 "\n"
                 "  --mcp      Serve MCP protocol (newline-delimited JSON-RPC)\n"
                 "  --lsp      Serve LSP protocol (Content-Length framed JSON-RPC)\n"
                 "  --help     Show this help message\n"
                 "  --version  Show version\n"
                 "\n"
                 "Default: auto-detect protocol from first input byte.\n");
}

static void printVersion()
{
    std::fprintf(stderr, "vbasic-server 0.1.0\n");
}

enum class Mode
{
    Mcp,
    Lsp,
    AutoDetect,
};

/// @brief Main event loop for MCP protocol.
static int runMcpServer(Transport &transport, BasicCompilerBridge &bridge)
{
    McpHandler handler(bridge, kBasicConfig);
    RawMessage msg;

    while (transport.readMessage(msg))
    {
        JsonValue json;
        try
        {
            json = JsonValue::parse(msg.content);
        }
        catch (const std::exception &)
        {
            transport.writeMessage(buildError(JsonValue(), kParseError, "Parse error"));
            continue;
        }

        JsonRpcRequest req;
        if (!parseRequest(json, req))
        {
            transport.writeMessage(buildError(JsonValue(), kInvalidRequest, "Invalid Request"));
            continue;
        }

        std::string response = handler.handleRequest(req);
        if (!response.empty())
            transport.writeMessage(response);
    }
    return 0;
}

/// @brief Main event loop for LSP protocol.
static int runLspServer(Transport &transport, BasicCompilerBridge &bridge)
{
    LspHandler handler(bridge, transport, kBasicConfig);
    RawMessage msg;

    std::fprintf(stderr, "[vbasic-server] LSP server started\n");
    std::fflush(stderr);

    while (transport.readMessage(msg))
    {
        std::fprintf(stderr, "[vbasic-server] recv %zu bytes\n", msg.content.size());
        std::fflush(stderr);

        JsonValue json;
        try
        {
            json = JsonValue::parse(msg.content);
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "[vbasic-server] parse error: %s\n", e.what());
            std::fflush(stderr);
            transport.writeMessage(buildError(JsonValue(), kParseError, "Parse error"));
            continue;
        }

        JsonRpcRequest req;
        if (!parseRequest(json, req))
        {
            std::fprintf(stderr, "[vbasic-server] invalid request\n");
            std::fflush(stderr);
            transport.writeMessage(buildError(JsonValue(), kInvalidRequest, "Invalid Request"));
            continue;
        }

        std::fprintf(stderr, "[vbasic-server] method=%s id=%s\n",
                     req.method.c_str(),
                     req.id.isNull() ? "null" : req.id.toCompactString().c_str());
        std::fflush(stderr);

        if (req.method == "exit")
            break;

        std::string response = handler.handleRequest(req);
        if (!response.empty())
        {
            std::fprintf(stderr, "[vbasic-server] resp %zu bytes\n", response.size());
            std::fflush(stderr);
            transport.writeMessage(response);
        }
        else
        {
            std::fprintf(stderr, "[vbasic-server] (no response — notification handled)\n");
            std::fflush(stderr);
        }
    }

    std::fprintf(stderr, "[vbasic-server] LSP server exiting\n");
    std::fflush(stderr);
    return 0;
}

int main(int argc, char **argv)
{
    platformInitStdio();

    Mode mode = Mode::AutoDetect;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--mcp") == 0)
            mode = Mode::Mcp;
        else if (std::strcmp(argv[i], "--lsp") == 0)
            mode = Mode::Lsp;
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0)
        {
            printUsage();
            return 0;
        }
        else if (std::strcmp(argv[i], "--version") == 0)
        {
            printVersion();
            return 0;
        }
        else
        {
            std::fprintf(stderr, "Unknown option: %s\n", argv[i]);
            printUsage();
            return 1;
        }
    }

    // Auto-detect: peek at first byte
    if (mode == Mode::AutoDetect)
    {
        int c = std::fgetc(stdin);
        if (c == EOF)
            return 0;
        std::ungetc(c, stdin);

        if (c == 'C' || c == 'c')
            mode = Mode::Lsp;
        else
            mode = Mode::Mcp;
    }

    BasicCompilerBridge bridge;

    std::unique_ptr<Transport> transport;
    if (mode == Mode::Lsp)
    {
        transport = std::make_unique<LspTransport>(stdin, stdout);
        return runLspServer(*transport, bridge);
    }
    else
    {
        transport = std::make_unique<McpTransport>(stdin, stdout);
        return runMcpServer(*transport, bridge);
    }
}
