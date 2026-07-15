---
status: active
audience: public
last-verified: 2026-07-14
---

# Zia Tooling

> In-process Zia compiler services for editors and IDE tooling.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Zia.Toolchain](#viperziatoolchain)
- [Viper.Zia.SemanticJob](#viperziasemanticjob)
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
| `BeginCheckForFile` | `Object(String, String)` | Start the path-aware analysis on a semantic worker and return a `SemanticJobHandle`. |
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
| `hasFixit` | Boolean | True when at least one machine-applicable fix is attached. |
| `fixits` | Object | `Seq` of all fix-it maps; each has `message`, `replacement`, and 1-based `startLine`, `startColumn`, `endLine`, and `endColumn`. |
| `fixitMessage` | String | Compatibility copy of the first fix-it message, or empty. |
| `fixitReplacement` | String | Compatibility copy of the first replacement, or empty. |
| `fixitStartLine` / `fixitStartColumn` | Integer | Compatibility copy of the first fix-it start, or 0. |
| `fixitEndLine` / `fixitEndColumn` | Integer | Compatibility copy of the first fix-it end, or 0. |

`Check*` and `Compile*` include both the complete `fixits` sequence and the
legacy first-fix fields. Diagnostics materialized from an asynchronous
`SemanticJob` currently include `hasFixit` and the legacy first-fix fields, but
not the `fixits` sequence.

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
        if Seq.get_Count(diagnostics) > 0 {
            var first = Seq.Get(diagnostics, 0);
            Terminal.Say(Map.GetStr(first, "file") + ":" + Map.GetInt(first, "line")
                + ": " + Map.GetStr(first, "message"));
        }
    }
}
```

### Notes

- Use `CheckForFile`, `BeginCheckForFile`, and `CompileForFile` from editors so relative `bind` paths resolve against the active document.
- The legacy `Viper.Zia.Completion.CheckForFile` API still returns tab-delimited text for compatibility. New IDE surfaces should consume `Viper.Zia.Toolchain` instead.
- The weak runtime stub returns empty diagnostics, a null async-job handle, and a failed compile result when the editor-service bridge is absent.
- Statically linked IDE hosts must force-load `zia_editor_services` (which links `fe_zia`) so its strong `rt_zia_*` definitions override the weak runtime stubs. The repository's `zia` executable does this with the platform-equivalent whole-archive option.

---

## Viper.Zia.SemanticJob

Pollable background language-service jobs returned by
`Viper.Zia.Completion.Begin*ForFile` and `Viper.Zia.Toolchain.BeginCheckForFile`.

**Type:** Static utility class operating on `SemanticJobHandle` objects

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `IsDone(job)` | `Boolean(Object)` | True when the background job has completed or the weak stub has no work to run. |
| `IsError(job)` | `Boolean(Object)` | True when a completed job has an error payload. |
| `ErrorOption(job)` | `Option[String](Object)` | Preferred error accessor: `SomeStr(message)` when an error exists, otherwise `None`. |
| `Error(job)` | `String(Object)` | Compatibility accessor that returns `""` when no error is present. |
| `Kind(job)` | `Integer(Object)` | Numeric semantic job kind. |
| `Cancel(job)` | `Void(Object)` | Request cancellation; running work may finish later. |
| `CompletionItems(job)` | `Seq(Object)` | Materialize completion results for completion jobs. |
| `SignatureInfo(job)` | `Map(Object)` | Materialize signature-help results for signature jobs. |
| `HoverInfo(job)` | `Map(Object)` | Materialize hover results for hover jobs. |
| `Symbols(job)` | `String(Object)` | Materialize serialized symbol rows for symbol jobs. |
| `Tokens(job)` | `String(Object)` | Materialize serialized semantic-token rows for token jobs. |
| `Diagnostics(job)` | `Seq(Object)` | Materialize diagnostic maps for diagnostics jobs. |

Prefer `ErrorOption(job)` in new editor code. It avoids treating an empty string
as a status sentinel and matches the runtime's Option-based absence model.

`Kind(job)` returns `0` unknown, `1` completion items, `2` signature info, `3`
hover info, `4` symbols, `5` diagnostics, or `6` semantic tokens. Result
materializers return an empty/default payload until the job is complete or when
called for the wrong job kind. `Cancel(job)` marks the job cancelled; an
already-running worker can continue to completion, but its result is discarded.

---

## Viper.Zia.ProjectIndex

Project-wide semantic navigation for Zia source buffers. The index stores
editor-supplied source text, including unsaved dirty buffers, and uses that text
when resolving `bind` imports.

**Type:** Static utility class returning an opaque
`Viper.Zia.ProjectIndex.ProjectIndexHandle`

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
| `reason` | String | Miss reason: `not_found`, `invalid_index`, or `unavailable` from the weak stub. |
| `file` | String | On success, normalized source path for the definition. |
| `line` | Integer | On success, 1-based definition start line. |
| `column` | Integer | On success, 1-based definition start column. |
| `endLine` | Integer | On success, 1-based definition end line. |
| `endColumn` | Integer | On success, 1-based exclusive definition end column. |
| `editorLine` | Integer | On success, 0-based start line for `CodeEditor`. |
| `editorColumn` | Integer | On success, 0-based start column for `CodeEditor`. |
| `editorEndLine` | Integer | On success, 0-based end line for `CodeEditor`. |
| `editorEndColumn` | Integer | On success, 0-based exclusive end column for `CodeEditor`. |
| `name` | String | On success, source-visible symbol name. |
| `semanticName` | String | On success, internal semantic name used for comparison. |
| `kind` | String | On success, `variable`, `parameter`, `function`, `method`, `field`, `type`, or `module`. |
| `type` | String | On success, display type when known. |
| `ownerType` | String | On success, enclosing type for members when known. |

Miss maps contain only `found = false` and `reason`; the location and symbol
fields are success-only.

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
| `reason` | String | Empty on success; otherwise `invalid_index`, `invalid_name`, `not_found`, `collision`, or weak-stub `unavailable`. |
| `name` | String | Original source-visible name; present on success only. |
| `newName` | String | Requested replacement; present on success only. |
| `references` | Object | `Seq` of reference maps used to build the edit set; present on success only. |
| `edits` | Object | `Seq` of workspace edit maps; present on success and failure. |

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
- Native IDE builds must link and force-load `zia_editor_services`; the weak runtime stub returns an invalid handle and empty or `unavailable` results.

---

## See Also

- [Collections](collections/README.md) - `Map` and `Seq` result containers
- [Diagnostics](diagnostics.md) - runtime assertion and trap helpers
