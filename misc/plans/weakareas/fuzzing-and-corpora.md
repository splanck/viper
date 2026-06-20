# Fuzzing: Regular Lane, Corpora, and Coverage of New Surfaces

**Status:** Completed — fuzz harness discovery, seed corpora, additional protocol/parser
harnesses, local mutation smoke, and CTest self-checks are in place.
**Area:** `src/tests/fuzz/`, `scripts/`
**Effort:** M
**Roadmap fit:** v0.2.x hardening

## Problem

13 fuzz harnesses exist (Zia/BASIC lexer+parser, IL parser, glTF/FBX/OBJ-STL/VSCN
loaders, locale-JSON/date-pattern/plural-rule parsers, stream manifest), but:

- They are **opt-in** (`VIPER_ENABLE_FUZZ=ON`) and not run on any regular cadence.
- Only the 3D/asset loader harnesses currently have committed corpora; Zia/BASIC/IL and
  localization parser harnesses ship no seed corpus.
- Key input surfaces are **unfuzzed**: `Viper.Text.Json`, LSP/MCP JSON-RPC framing/parsing,
  and selected network/protocol parsers such as HTTP request parsing. Bytecode has an
  in-memory module format; add a binary-deserializer fuzzer only if/when a serialized
  bytecode loader is present.

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
2. **New harnesses:** `fuzz_json` (the runtime JSON parser), `fuzz_http_request`, and
   `fuzz_lsp_jsonrpc` / `fuzz_mcp_jsonrpc` (protocol framing/parsing). Add
   `fuzz_bytecode_binary` only after confirming a real serialized bytecode loader; do not
   fuzz imaginary input formats.
3. **`scripts/fuzz_smoke.sh`:** iterate harnesses, time-box each, fail on a new crash;
   on crash, write the minimized reproducer next to the corpus.
4. **ctest replay:** register a `fuzz-replay` test per harness that runs it once over its
   corpus (no mutation), labelled `fuzz`. Use libFuzzer's corpus replay mode when the
   toolchain supports it; otherwise compile a small deterministic replay wrapper.
5. Add a CMake/source-of-truth helper that discovers fuzz targets from
   `src/tests/fuzz/CMakeLists.txt` so scripts and docs do not duplicate the target list.

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

## Implementation notes

- Seed corpora now exist for every discovered harness under
  `src/tests/fuzz/corpus/<harness>/`.
- New harnesses cover JSON, HTTP request parsing, LSP JSON-RPC, and MCP JSON-RPC.
- `scripts/fuzz_smoke.sh` discovers targets from `src/tests/fuzz/CMakeLists.txt`, checks
  corpus coverage, lists targets, and runs a time-boxed libFuzzer smoke lane.
- `fuzz_smoke_self_test` is registered in CTest for deterministic discovery/corpus
  validation.
- `docs/testing.md` and `docs/contributor-guide.md` document replay vs exploration and
  the harness/corpus workflow.

## Verification

- `./scripts/fuzz_smoke.sh --self-test`
- `ctest --test-dir build -R '^fuzz_smoke_self_test$' --output-on-failure`

## Risks / open questions

- **Corpus size in-repo** — keep seeds small and minimized; store only reduced reproducers.
- **Confirm the binary-IL surface** before writing `fuzz_il_binary`; if only text IL
  exists, drop that harness from scope.
