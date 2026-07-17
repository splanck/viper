---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0064: IL Spec Version 0.2.0 → 0.3.0

## Status

Accepted

## Context

The IL grammar and opcode surface have grown materially since `0.2.0` was cut.
The additions are backward-compatible for a reader (nothing that parsed under
0.2.0 stops parsing) but forward-incompatible for a writer: a module that uses
the new surface cannot be consumed by an older toolchain. The accumulated
changes since 0.2.0 include:

- **`select`** — a branchless conditional-value opcode with strict, pure
  semantics, lowered to `csel`/`cmov` and exercised by the `if-conv` pass
  (ADR 0063).
- **`switch.i32`** — a multi-way integer dispatch (jump-table) terminator with
  its own operand-parse form.
- **Checked / narrow integer arithmetic** — the overflow-checked opcodes
  (`iadd.ovf`/`isub.ovf`/`imul.ovf` family) together with the range-analysis
  demotion proofs that let the verifier re-prove a checked op can be demoted to
  its plain form (ADR 0026), and the narrower-width arithmetic lowering paths.

`docs/il/il-guide.md#reference` is normative, and IL grammar/opcode changes are
ADR-gated (ADR 0006). Rather than fold each addition into a lingering `0.2.0`
banner, this ADR advances the spec version so the version line honestly
reflects the surface a module may use.

The IL version is a **descriptive marker, not an enforced gate**. The text
parser (`src/il/io/ModuleParser.cpp`) stores the `il <version>` banner value
verbatim and never validates it; there is no supported-range check anywhere in
`src/il/`. This has been true since before 0.2.0 (the corpus already mixes
`il 0.1` and `il 0.2.0` fixtures, all of which parse).

## Decision

1. **Advance the IL spec version to `0.3.0`.** The single source of truth is
   `src/buildmeta/IL_VERSION`; CMake bakes it into `ZANNA_IL_VERSION_STR`
   (`include/zanna/version.hpp.in`), which flows to `Module::version`'s default,
   the `il <version>` serialized banner, and every tool's version output.

2. **Renumber only — no new gating.** The parser and verifier are unchanged.
   Existing `il 0.2.0` (and older) modules continue to parse and run. The
   toolchain does not reject or warn on a version mismatch, so `IL current` and
   `IL supported` remain the same value; a real supported-range check would be a
   separate grammar/verifier change requiring its own ADR.

3. **Serialized default flips to `il 0.3.0`.** Because `Module::version`
   defaults to `ZANNA_IL_VERSION_STR`, any freshly-built module now serializes
   with an `il 0.3.0` banner. The two golden fixtures compared byte-for-byte
   against a freshly-built module (`src/tests/golden/hello_expected.il`,
   `src/tests/golden/il/serializer_all_opcodes.il`) were regenerated; version-
   normalized and parse-input goldens were unaffected but were swept to `0.3.0`
   for consistency.

4. **Bytecode format version is independent and unchanged.**
   `kBytecodeVersion` (`src/bytecode/Bytecode.hpp`) is a separate axis and does
   not move with the IL spec version.

## Consequences

- The `il <version>` banner now truthfully names the surface (`select`,
  `switch.i32`, checked/narrow arithmetic) that a 0.3.0 module may contain.
- No behavioral change for existing programs: parsing, verification, and
  execution of 0.2.0 modules are byte-for-byte identical.
- Because there is no gate, a 0.3.0 module fed to a hypothetical older toolchain
  would fail later (at an unknown-opcode error), not at the banner. Adding an
  explicit version gate is deferred to a future ADR if/when cross-version
  interop becomes a requirement.
- Documentation that states the IL version (`docs/il/il-guide.md`,
  `docs/internals/architecture.md`, `README.md`, and the code-block banners throughout the
  docs/examples/tests corpus) was updated to `0.3.0` in the same change.
