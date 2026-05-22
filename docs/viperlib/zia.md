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
- [Viper.Zia.ProjectIndex](#viperziaprojectindex)

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

## Viper.Zia.ProjectIndex

Project-wide semantic navigation for Zia source buffers. The index stores
editor-supplied source text, including unsaved dirty buffers, and uses that text
when resolving `bind` imports.

**Type:** Static utility class returning an opaque `ProjectIndex.Handle`

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `New` | `Object(String)` | Create a project index rooted at the supplied project directory. |
| `IsValid` | `Boolean(Object)` | Return true when the handle still owns a native index. |
| `UpdateFile` | `Boolean(Object, String, String)` | Store current source text for a file path. This may be unsaved editor text. |
| `RemoveFile` | `Boolean(Object, String)` | Remove one file from the index. |
| `Clear` | `Void(Object)` | Remove every indexed file. |
| `Destroy` | `Void(Object)` | Dispose the native index payload. The handle becomes inert. |
| `Definition` | `Object(Object, String, String, Integer, Integer)` | Return a definition map for the identifier at `line`, `col`. |
| `References` | `Object(Object, String, String, Integer, Integer)` | Return a `Seq` of semantic reference maps. |
| `RenameEdits` | `Object(Object, String, String, Integer, Integer, String)` | Return workspace edits for a semantic rename without changing files. |

`Definition`, `References`, and `RenameEdits` also update the queried file with
the supplied source text before analysis. Cursor `line` is 1-based and cursor
`col` is 0-based, matching the existing completion APIs.

### Definition Map

`Definition` returns a `Viper.Collections.Map`.

| Key | Type | Description |
|-----|------|-------------|
| `found` | Boolean | True when a semantic definition was found. |
| `reason` | String | Present for misses such as `not_found` or `invalid_index`. |
| `file` | String | Normalized source path for the definition. |
| `line` | Integer | 1-based definition start line. |
| `column` | Integer | 1-based definition start column. |
| `endLine` | Integer | 1-based definition end line. |
| `endColumn` | Integer | 1-based exclusive definition end column. |
| `editorLine` | Integer | 0-based start line for `CodeEditor`. |
| `editorColumn` | Integer | 0-based start column for `CodeEditor`. |
| `editorEndLine` | Integer | 0-based end line for `CodeEditor`. |
| `editorEndColumn` | Integer | 0-based exclusive end column for `CodeEditor`. |
| `name` | String | Source-visible symbol name. |
| `semanticName` | String | Internal semantic name used for comparison. |
| `kind` | String | `variable`, `parameter`, `function`, `method`, `field`, `type`, or `module`. |
| `type` | String | Display type when known. |
| `ownerType` | String | Enclosing type for members when known. |

### Reference Map

`References` returns a `Seq` of maps with:

| Key | Type | Description |
|-----|------|-------------|
| `file` | String | Normalized source path containing the reference. |
| `line` / `column` | Integer | 1-based reference start. |
| `endLine` / `endColumn` | Integer | 1-based exclusive reference end. |
| `editorLine` / `editorColumn` | Integer | 0-based reference start for editor operations. |
| `editorEndLine` / `editorEndColumn` | Integer | 0-based exclusive reference end for editor operations. |
| `name` | String | Source-visible symbol name. |
| `semanticName` | String | Internal semantic name. |
| `kind` | String | Symbol kind. |
| `isDefinition` | Boolean | True when the reference is the declaration token. |

Reference lookup is semantic, not string search: it resolves identifiers through
the Zia analyzer, ignores comments and string literals, and separates shadowed
locals from globals/imports.

### Rename Result Map

`RenameEdits` returns a `Viper.Collections.Map`.

| Key | Type | Description |
|-----|------|-------------|
| `success` | Boolean | True when edits were produced. |
| `reason` | String | Empty on success; otherwise `invalid_index`, `invalid_name`, `not_found`, or `collision`. |
| `name` | String | Original source-visible name on success. |
| `newName` | String | Requested replacement on success. |
| `references` | Object | `Seq` of reference maps used to build the edit set. |
| `edits` | Object | `Seq` of workspace edit maps. |

Each edit map contains `file`, `startLine`, `startColumn`, `endLine`,
`endColumn`, `editorStartLine`, `editorStartColumn`, `editorEndLine`,
`editorEndColumn`, and `newText`. Rename only returns edits; callers must apply
them to editor buffers or files themselves.

### ProjectIndex Example

```rust
module ProjectIndexDemo;

bind Viper.Zia.ProjectIndex as ProjectIndex;
bind Viper.Collections.Map as Map;
bind Viper.Terminal as Terminal;

func start() {
    var index = ProjectIndex.New(".");
    var source = "module Demo;\nfunc start() { var answer = 42; answer; }\n";
    ProjectIndex.UpdateFile(index, "demo.zia", source);

    var definition = ProjectIndex.Definition(index, "demo.zia", source, 2, 37);
    if Map.GetBool(definition, "found") {
        Terminal.Say(Map.GetStr(definition, "file") + ":" + Map.GetInt(definition, "line"));
    }

    ProjectIndex.Destroy(index);
}
```

### Notes

- Always call `UpdateFile` when a buffer changes. Query calls also accept current source to cover the active dirty buffer.
- Add all open project files to the index for best reference and rename results. Files absent from the index can still be resolved from disk through normal `bind` loading, but they are not scanned for references.
- Native IDE builds must link `fe_zia`; the weak runtime stub returns an invalid handle and empty results.

---

## See Also

- [Collections](collections/README.md) - `Map` and `Seq` result containers
- [Diagnostics](diagnostics.md) - runtime assertion and trap helpers
