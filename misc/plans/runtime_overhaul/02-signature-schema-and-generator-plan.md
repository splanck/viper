# Signature Schema And Generator Plan

## Problem

The live API dump has fixed the old public `ptr` leak, but it still mixes public
signature dialects:

- Global function rows contain both `str` and `string`.
- Class method/property rows use `str`.
- Sequence-like returns appear as both `seq<T>` and exact object spellings such
  as `obj<Viper.Collections.Seq>`.

This is a tooling problem. Agents, docs generators, language servers, and API
compatibility checks need one stable schema.

## Decision

Use `runtime.def` public signature text as the machine-readable dialect:

```text
void
i1
i64
f64
str
obj
obj<Viper.Type>
seq<T>
```

`string` is reserved for human documentation prose, not the machine catalog.
`str?` and other suffix-nullable spellings are not part of the dialect unless a
future schema revision formally adds nullable types.

## Target JSON Shape

Add explicit schema metadata to `--dump-runtime-api`:

```json
{
  "version": "0.2.99.20260704",
  "schema_version": 1,
  "signature_dialect": "runtime-def-v1",
  "functions": [
    {
      "name": "Viper.Core.Parse.TryInt",
      "signature": "obj<Viper.Option>(str)"
    }
  ],
  "classes": [
    {
      "name": "Viper.Core.Parse",
      "constructor": "",
      "properties": [],
      "methods": [
        {
          "name": "TryInt",
          "signature": "obj<Viper.Option>(str)"
        }
      ]
    }
  ]
}
```

Future versions can add parsed types without removing the compact signature:

```json
{
  "signature": "obj<Viper.Option>(str)",
  "return_type": {"kind": "object", "class": "Viper.Option"},
  "params": [{"kind": "string"}]
}
```

The parsed form should also carry contract metadata that cannot be recovered
from the compact string:

```json
{
  "signature": "obj<Viper.Result>(str,i64)",
  "return_type": {"kind": "object", "class": "Viper.Result", "result_of": "Viper.Network.Socket"},
  "params": [
    {"name": "host", "type": {"kind": "string"}},
    {"name": "timeout", "type": {"kind": "integer"}, "unit": "milliseconds"}
  ],
  "fallibility": "result",
  "stability": "preview",
  "capabilities": ["network"],
  "docs_anchor": "docs/viperlib/network.md#socket-connect"
}
```

## Generator Work

1. Identify where globals are converted from `str` to `string` in
   `src/tools/rtgen/rtgen.cpp`.
2. Preserve public signature text for frontend-visible `RT_FUNC` rows.
3. Keep lowered IL/native ABI descriptors internal to generated runtime tables.
4. Make `src/tools/viper/main.cpp` print the preserved public text for both
   globals and class entries.
5. Add schema metadata to the dump.
6. Add a parser test for representative signatures:
   - `str(i64)`
   - `obj<Viper.Option>(str)`
   - `seq<str>(obj)`
   - `obj<Viper.Collections.Seq>(obj)`
   - `void(obj,i64,i1)`

## Normalization Rules

### String

Canonical machine spelling: `str`.

Audit:

- Fail if public dump contains `string(`, `, string`, or `: string` in a
  signature.
- Human docs may continue to say `String`.

### Nullable values

Current second-pass source finding:

- `Viper.Terminal.Ask` uses `str?(str)`.
- `Viper.Terminal.ReadLine` uses `str?()`.

Problem: `str?` is not declared in the public signature dialect. Tools that
parse `runtime.def` signatures cannot know whether this means nullable string,
optional string, or a legacy frontend hint.

Decision:

- For the overhaul, convert these to `Option<str>` or `Result<str>` depending on
  the actual distinction between EOF, cancellation, terminal unavailable, and
  valid empty input.
- Add a public-signature parser check that fails on `?` suffixes until nullable
  types are formally specified.
- If a future nullable type is wanted, add it as schema version 2 with parsed
  metadata and docs before using it in public APIs.

### Boolean

Canonical machine spelling: `i1`.

Audit:

- Existing boolean probe audit remains.
- Add checks for setter names such as `SetVisible`, `SetEnabled`, `SetLoop`,
  `SetChecked`, and `SetUnlit` to keep boolean parameters as `i1`.

### Object

Canonical machine spelling:

- `obj` for untyped object values.
- `obj<Viper.Type>` for a promised runtime class.

Audit:

- Fail if public signatures contain `ptr`.
- Fail if a class method promises a typed object but its global function emits
  an untyped object for the same handler, unless an exception is documented.

### Sequence

Decision:

- `seq<T>` means callers can depend on sequence element type.
- `obj<Viper.Collections.Seq>` means callers only get the sequence object.

Work item:

- Review every `Keys`, `Values`, `ToSeq`, and list-returning API.
- Convert to `seq<T>` where the runtime contract is typed.
- Leave `obj<Viper.Collections.Seq>` only when the exact object class matters or
  the contents are heterogeneous.

### Numeric domains and units

The compact dialect cannot tell whether an `i64` is a key code, byte count,
milliseconds, HTTP status, log level, color, layer mask, or opaque handle.

Work item:

- Add optional parameter and return metadata for units and enum domains.
- Treat missing units on timeout/duration APIs as audit failures.
- Treat missing domains on constant-like integer groups as audit warnings until
  the enum/value-object migration is complete.

## Compatibility Check

Add a generated snapshot test:

```sh
./build/src/tools/viper/viper --dump-runtime-api > build/runtime-api.json
```

The test should assert:

- JSON parses.
- `schema_version` is present.
- no public `ptr`
- no public `string`
- class/global signatures for the same runtime handler use the same public
  dialect.

## Implementation Risks

- Some frontend code may currently compare textual `string` signatures.
- Generated headers may use public text in places that expect lowered ABI text.
- Tests must distinguish public signature strings from internal lowering
  descriptors.

Mitigation: keep two named fields internally: `publicSignatureText` and
`loweredSignature`.

## Acceptance Criteria

- `--dump-runtime-api` emits one dialect.
- Runtime docs generator, if present, consumes the same dialect.
- `./scripts/audit_runtime_surface.sh` passes.
- `./scripts/check_runtime_completeness.sh` passes.
- New signature dialect test fails on `ptr` or `string` in public JSON.
