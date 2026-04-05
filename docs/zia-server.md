---
status: active
audience: public
last-verified: 2026-04-05
---

# Zia Language Server Reference

`zia-server` is a language server for the Zia programming language. It exposes the Zia compiler's analysis, completion, and runtime APIs over two standard protocols:

- **MCP (Model Context Protocol)** — for AI assistants (Claude Code, Copilot, etc.)
- **LSP (Language Server Protocol)** — for editors (VS Code, Neovim, etc.)

Both protocols share a single compiler bridge and run over stdin/stdout JSON-RPC 2.0.

## Architecture

```text
┌──────────────┐  ┌──────────────┐
│  MCP Handler │  │  LSP Handler │     ← Protocol-specific dispatch
└──────┬───────┘  └──────┬───────┘
       │                 │
       └────────┬────────┘
                │
       ┌────────▼────────┐
       │ Compiler Bridge  │             ← Shared compiler facade
       └────────┬────────┘
                │
    ┌───────────┼───────────┐
    │           │           │
┌───▼───┐  ┌───▼───┐  ┌───▼───────────┐
│fe_zia │  │il_io  │  │RuntimeRegistry│  ← Existing Viper libraries
└───────┘  └───────┘  └───────────────┘
```

## CLI Reference

```bash
# Start in MCP mode (for Claude Code, Copilot, etc.)
zia-server --mcp

# Start in LSP mode (for VS Code)
zia-server --lsp

# Auto-detect protocol from first input byte
zia-server

# Show help
zia-server --help

# Show version
zia-server --version
```

| Flag | Description |
|------|-------------|
| `--mcp` | Serve MCP protocol (newline-delimited JSON-RPC) |
| `--lsp` | Serve LSP protocol (Content-Length framed JSON-RPC) |
| `--help` | Show usage information |
| `--version` | Show version string |

**Auto-detection:** When no flag is given, the server peeks at the first byte of input. If it starts with `C` (indicating a `Content-Length:` header), LSP mode is selected. Otherwise, MCP mode is assumed.

---

## MCP Mode

### Configuration for Claude Code

Add to your project's `.mcp.json` or global MCP settings:

```json
{
  "mcpServers": {
    "zia": {
      "command": "/path/to/zia-server",
      "args": ["--mcp"]
    }
  }
}
```

### MCP Lifecycle

1. Client sends `initialize` → server returns protocol version and capabilities
2. Client sends `initialized` (notification) → no response
3. Client sends `tools/list` → server returns 11 tool definitions
4. Client sends `tools/call` with tool name and arguments → server returns result

### MCP Tools

| Tool | Description | Required Args |
|------|-------------|---------------|
| `zia/check` | Type-check source, return diagnostics | `source` |
| `zia/compile` | Full compilation with success/fail status | `source` |
| `zia/completions` | Code completions at cursor position | `source`, `line`, `col` |
| `zia/hover` | Type information for symbol at cursor | `source`, `line`, `col` |
| `zia/symbols` | List top-level declarations | `source` |
| `zia/dump-il` | Dump compiled IL | `source` |
| `zia/dump-ast` | Dump abstract syntax tree | `source` |
| `zia/dump-tokens` | Dump token stream | `source` |
| `zia/runtime-classes` | List all runtime classes | _(none)_ |
| `zia/runtime-methods` | List members of a runtime class | `className` |
| `zia/runtime-search` | Search runtime APIs by keyword | `keyword` |

All source-based tools accept an optional `path` argument (defaults to `"untitled.zia"`).
`zia/dump-il` accepts an optional `optimized` boolean (defaults to `false`).

For detailed JSON schemas and examples, see [MCP Tool Specification](zia-server-mcp-tools.md).

### Example MCP Session

```jsonl
→ {"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}
← {"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2024-11-05","capabilities":{"tools":{}},"serverInfo":{"name":"zia-server","version":"0.1.0"}}}

→ {"jsonrpc":"2.0","method":"initialized"}

→ {"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"zia/check","arguments":{"source":"module Test;\nfunc start() {\n    Viper.Terminal.Say(\"hello\");\n}\n"}}}
← {"jsonrpc":"2.0","id":2,"result":{"content":[{"type":"text","text":"[]"}]}}
```

---

## LSP Mode

### VS Code Configuration

Create a VS Code extension or add to your `settings.json`:

```json
{
  "zia.server.path": "/path/to/zia-server",
  "zia.server.args": ["--lsp"]
}
```

Or configure as a language server in a VS Code extension's `activate()`:

```typescript
const serverOptions = {
  command: '/path/to/zia-server',
  args: ['--lsp'],
};
const clientOptions = {
  documentSelector: [{ scheme: 'file', language: 'zia' }],
};
const client = new LanguageClient('zia', 'Zia Language Server', serverOptions, clientOptions);
client.start();
```

### Supported LSP Capabilities

| Capability | Description |
|------------|-------------|
| `textDocumentSync: Full` | Client sends complete document text on every change |
| `completionProvider` | Trigger character: `.` |
| `hoverProvider` | Type information on hover |
| `documentSymbolProvider` | Top-level declarations outline |
| `publishDiagnostics` | Errors and warnings pushed on open/change |

### Diagnostics

Diagnostics are published automatically when a document is opened or changed. The severity mapping is:

| Zia Severity | LSP Severity |
|--------------|--------------|
| Note (0) | Information (3) |
| Warning (1) | Warning (2) |
| Error (2) | Error (1) |

---

## Troubleshooting

**Binary not found:** Ensure the build completed successfully and the binary is at `build/src/tools/zia-server/zia-server`.

**No completions after ".":** Check that the document is open (LSP) or that the `source` argument contains the text up to and including the `.` character (MCP).

**Windows stdio issues:** The server sets binary mode on stdin/stdout automatically. If you see corrupted output, ensure your client isn't adding extra CR/LF conversion.

**Empty hover results:** The current hover implementation checks global symbols by line number. Hover on local variables may return empty — this is a known limitation that will improve with AST-cursor-walking support.
