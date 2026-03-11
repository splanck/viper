---
status: active
audience: contributors
last-verified: 2026-03-10
---

# CODEMAP: Zia Language Server

Source files for `zia-server`, the dual-protocol language server for Zia (`src/tools/zia-server/`).

## Overview

- **Total source files**: 16 (.hpp/.cpp)
- **Architecture**: 3 layers — Transport/JSON-RPC → Protocol Handlers → Compiler Bridge
- **Protocols**: MCP (newline-delimited) and LSP (Content-Length framed)

## Source Files

### Foundation Layer

| File | Purpose |
|------|---------|
| `Json.hpp` | JSON value type (`std::variant`-based), parser, and emitter |
| `Json.cpp` | Recursive-descent JSON parser, RFC 8259 compliance |
| `Transport.hpp` | Abstract transport + `McpTransport` / `LspTransport` |
| `Transport.cpp` | Newline-delimited (MCP) and Content-Length framed (LSP) I/O |
| `JsonRpc.hpp` | JSON-RPC 2.0 request/response types, error codes |
| `JsonRpc.cpp` | JSON-RPC parsing and response building |

### Protocol Handlers

| File | Purpose |
|------|---------|
| `McpHandler.hpp` | MCP lifecycle + 11 tool definitions |
| `McpHandler.cpp` | MCP dispatch: initialize, tools/list, tools/call |
| `LspHandler.hpp` | LSP capabilities, request/notification handlers |
| `LspHandler.cpp` | LSP dispatch: initialize, completion, hover, symbols, diagnostics |
| `DocumentStore.hpp` | In-memory URI→content map for LSP open files |
| `DocumentStore.cpp` | URI management, %XX decoding, document lifecycle |

### Compiler Bridge

| File | Purpose |
|------|---------|
| `CompilerBridge.hpp` | Protocol-agnostic facade, data transfer structs |
| `CompilerBridge.cpp` | Wraps `fe_zia` APIs: analysis, completion, IL dump, runtime queries |

### Entry Point

| File | Purpose |
|------|---------|
| `main.cpp` | CLI parsing, auto-detection, event loop for MCP/LSP |

## Dependencies

```text
zia-server → zia_server_lib → fe_zia, il_full, il_transform
                             → viper_support (diagnostics, source manager)
                             → il_runtime (RuntimeClasses, RuntimeRegistry)
```

## Data Flow

```text
stdin → Transport.readMessage()
      → JsonValue::parse()
      → parseRequest() → JsonRpcRequest
      → McpHandler/LspHandler.handleRequest()
      → CompilerBridge.method()
      → fe_zia API (parseAndAnalyze, compile, CompletionEngine)
      → response string
      → Transport.writeMessage() → stdout
```

## Test Files

| File | Tests | Purpose |
|------|-------|---------|
| `test_json.cpp` | 60 | JSON parser/emitter: types, nesting, escapes, round-trip |
| `test_transport.cpp` | 18 | MCP/LSP framing, JSON-RPC parse/build |
| `test_compiler_bridge.cpp` | 24 | Bridge methods against real Zia snippets |
| `test_mcp_handler.cpp` | 18 | MCP lifecycle + tool dispatch integration |
| `test_lsp_handler.cpp` | 16 | LSP lifecycle + feature handler integration |
