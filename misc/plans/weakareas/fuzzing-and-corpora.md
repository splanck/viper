# Fuzzing: Regular Lane, Corpora, and Coverage of New Surfaces

**Status:** Verified real (harnesses exist but are opt-in, under-corpus'd, and miss key surfaces)
**Area:** `src/tests/fuzz/`, `scripts/`
**Effort:** M
**Roadmap fit:** v0.2.x hardening

## Problem

13 fuzz harnesses exist (Zia/BASIC lexer+parser, IL parser, glTF/FBX/OBJ-STL/VSCN
loaders, locale-JSON/date-pattern/plural-rule parsers, stream manifest), but:

- They are **opt-in** (`VIPER_ENABLE_FUZZ=ON`) and not run on any regular cadence.
- Several harnesses **ship no seed corpus** (e.g. the IL parser, locale/date parsers).
- Key input surfaces are **unfuzzed**: the JSON parser, any binary/bytecode deserializer,
  and network/protocol (HTTP request, LSP/MCP JSON-RPC) parsing.

## Goal & scope

- **In:** (a) Seed corpora for every corpus-less harness, (b) new harnesses for the
  unfuzzed surfaces, (c) a **local** `scripts/fuzz_smoke.sh` lane that time-boxes each
  harness over its corpus, and (d) a checked-in **crash-regression corpus** replayed as a
  fast, deterministic ctest so past crashes can't return.
- **Out:** Hosted/continuous fuzzing (OSS-Fuzz) and any GitHub Actions changes
  (prohibited this phase). Keep everything a local script + ctest replay.

## Design

Two cadences:
1. **Regression replay (in the normal suite):** run each harness once over its committed
   corpus (no mutation) — fast, deterministic, catches reintroduced crashes. This is the
   piece that belongs in `ctest`.
2. **Exploration (opt-in local lane):** `scripts/fuzz_smoke.sh` runs libFuzzer with
   mutation for N seconds per harness; new crashes are minimized and added to the
   regression corpus.

## Implementation steps

1. **Seed corpora:** for each corpus-less harness, mine inputs from existing tests
   (e.g. real `.il` files for the IL parser, locale JSON from the localization tests) into
   `src/tests/fuzz/corpus/<harness>/`.
2. **New harnesses:** `fuzz_json` (the JSON parser), `fuzz_http_request` /
   `fuzz_lsp_jsonrpc` (protocol framing/parsing), and — **if a binary IL/bytecode
   deserializer exists** (confirm; the text IL parser is already fuzzed) —
   `fuzz_il_binary`. Follow the existing harness structure.
3. **`scripts/fuzz_smoke.sh`:** iterate harnesses, time-box each, fail on a new crash;
   on crash, write the minimized reproducer next to the corpus.
4. **ctest replay:** register a `fuzz-replay` test per harness that runs it once over its
   corpus (no mutation), labelled `fuzz`; this runs in the normal suite.

## Tests

- The replay targets are themselves the regression tests — they must be green over the
  committed corpora.
- Self-check: a deliberately malformed input in a harness's corpus should be handled
  (no crash) — proves the harness exercises the guard.

## Documentation

- Update `docs/testing.md`: the two cadences, how to run `fuzz_smoke.sh`, how to add a
  harness + corpus, and the crash-triage/minimization workflow.
- Cross-reference from the contributor guide ("new parser/input surface ⇒ add a harness").

## Cross-platform

libFuzzer requires Clang (canonical here). The replay ctest is plain execution and runs
everywhere. Gate the mutation lane on a Clang/libFuzzer-capable toolchain.

## Risks / open questions

- **Corpus size in-repo** — keep seeds small and minimized; store only reduced reproducers.
- **Confirm the binary-IL surface** before writing `fuzz_il_binary`; if only text IL
  exists, drop that harness from scope.
