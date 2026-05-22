---
status: active
audience: public
last-verified: 2026-05-22
---

# Zia Tooling

> In-process Zia compiler services for editors and IDE tooling.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Zia.Toolchain](#viperziatoolchain)

---

## Viper.Zia.Toolchain

Structured diagnostics and compile results for Zia source buffers.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Check` | `Object(String)` | Analyze source and return a `Viper.Collections.Seq` of diagnostic maps. |
| `CheckForFile` | `Object(String, String)` | Analyze source using the supplied path for diagnostics and relative `bind` resolution. |
| `Compile` | `Object(String)` | Compile source and return a result map. |
| `CompileForFile` | `Object(String, String)` | Compile source using the supplied path for diagnostics and relative `bind` resolution. |

### Diagnostic Map

`Check*` returns a `Seq` whose entries are `Viper.Collections.Map` objects.
`Compile*` returns the same sequence under the `diagnostics` field.

| Key | Type | Description |
|-----|------|-------------|
| `file` | String | Normalized source path, or the requested path when no compiler file id is available. |
| `line` | Integer | 1-based start line, or 0 when unavailable. |
| `column` | Integer | 1-based start column, or 0 when unavailable. |
| `endLine` | Integer | 1-based end line, or the start line when no range is available. |
| `endColumn` | Integer | 1-based end column, or the start column when no range is available. |
| `severity` | Integer | `0` error, `1` warning, `2` note. |
| `severityName` | String | `error`, `warning`, or `note`. |
| `code` | String | Stable diagnostic code when one is available. |
| `message` | String | Human-readable diagnostic text. |
| `stage` | String | Compiler stage when available. |
| `help` | String | Extra help text when available. |

### Compile Result Map

`Compile` and `CompileForFile` return a `Viper.Collections.Map` with:

| Key | Type | Description |
|-----|------|-------------|
| `success` | Boolean | True when compilation completed without error diagnostics. |
| `diagnostics` | Object | `Viper.Collections.Seq` of diagnostic maps. |
| `sourcePath` | String | Normalized primary source path. |
| `outputPath` | String | Reserved for future file-emitting compile flows. Currently empty. |
| `il` | String | Serialized IL when `success` is true; empty on failure. |

### Zia Example

```rust
module ToolingDemo;

bind Viper.Zia.Toolchain as Toolchain;
bind Viper.Collections.Map as Map;
bind Viper.Collections.Seq as Seq;
bind Viper.Terminal as Terminal;

func start() {
    var source = "module Demo;\nfunc f() -> Integer { return ; }\n";
    var result = Toolchain.CompileForFile(source, "demo.zia");

    if Map.GetBool(result, "success") == false {
        var diagnostics = Map.Get(result, "diagnostics");
        if Seq.get_Length(diagnostics) > 0 {
            var first = Seq.Get(diagnostics, 0);
            Terminal.Say(Map.GetStr(first, "file") + ":" + Map.GetInt(first, "line")
                + ": " + Map.GetStr(first, "message"));
        }
    }
}
```

### Notes

- Use `CheckForFile` and `CompileForFile` from editors so relative `bind` paths resolve against the active document.
- The legacy `Viper.Zia.Completion.CheckForFile` API still returns tab-delimited text for compatibility. New IDE surfaces should consume `Viper.Zia.Toolchain` instead.
- The weak runtime stub returns empty diagnostics and a failed compile result when `fe_zia` is not linked. Native IDE builds must force-load `fe_zia` to get real compiler services.

---

## See Also

- [Collections](collections/README.md) - `Map` and `Seq` result containers
- [Diagnostics](diagnostics.md) - runtime assertion and trap helpers
