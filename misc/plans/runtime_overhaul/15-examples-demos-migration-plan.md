# Examples And Demos Migration Plan

## Goal

Treat all examples, demos, snippets, and sample apps as public API consumers.
The runtime overhaul is not production-ready until these samples either migrate
to the new API or are explicitly marked as legacy/history.

This plan is documentation-only. It records the required future audit and
migration work; it does not run builds or tests.

## Current Sample Surfaces

Source-only inventory found these important areas:

| Area | Purpose |
|---|---|
| `examples/apiaudit/**` | Focused runtime API examples in Zia and BASIC. |
| `examples/3d/**` | 3D starters, probes, showcases, and baselines. |
| `examples/apps/**` | Larger application examples such as paint, telnet, webserver, varc, vipersql. |
| `examples/games/**` | Larger Zia and BASIC games. |
| `examples/vbasic/**` | BASIC language examples. |
| `examples/sqldb-basic/**` | Larger BASIC SQL engine example. |
| `examples/zia/**` | Zia language examples. |
| `examples/localization/**` | Localization demos. |
| `examples/il/**` | IL tutorial, debug, benchmark, and reference examples. |
| `examples/embedding/**` | Host C++ embedding examples. |
| `examples/bin/**` | Built or generated demo binaries/artifacts. |
| `tests/runtime/demo_*.zia` | Runtime 3D demo fixtures. |
| `misc/video/**` | Video/presentation demo scripts. |
| `baseball/demos/**` and `baseball/src/demo/**` | Nested demo material. |
| `src/lib/graphics/examples/**` | Lower-level graphics library C examples. |
| `docs/**` and `misc/site/**` snippets | Documentation examples and website snippets. |

There are roughly 980 files under `examples/` matching common source, docs, IL,
HTML, and manifest extensions. The migration must be staged.

## Existing Classification Surface

`examples/smoke_manifest.tsv` already classifies example coverage:

- `check-smoke` for fast source checks.
- `check` for full checks.
- `il-run-smoke` for runnable IL.
- `classified` for benchmark, graphical, project, or non-standalone examples.
- labels such as `examples`, `smoke`, `basic`, `zia`, `il`, `requires_display`,
  `slow`, and `perf`.

Future implementation slices should update this manifest when files move,
rename, or change from non-runnable to runnable.

## Migration Principles

- examples should show the canonical modern API.
- compatibility aliases appear only in migration examples.
- examples should check `Option` and `Result` explicitly.
- examples should avoid raw sentinel checks after replacement APIs exist.
- demos should use typed domains such as keys, colors, durations, status codes,
  and layer masks after those domains exist.
- security examples should be safe by default.
- large games/apps should be migrated after smaller API audit examples prove the
  new style.

## Breakage Categories To Audit

### Renames

Search examples for every old name in the rename backlog:

- abbreviations such as `LeadZ`, `TrailZ`, `BoolYN`, `SetDTMax`.
- constructor/factory moves such as `NewBox` to `Box`.
- `Put` collection methods that become `Set` or `Add`.
- duplicate key constants moving to canonical input names.

### Signature changes

Audit for:

- `Try*` value APIs moving to `Option`.
- fallible APIs moving to `Result`.
- `str?` terminal APIs moving to option/result forms.
- high-arity calls replaced by option/value objects.
- duration or timeout parameters gaining `Duration` or unit-suffixed names.

### Namespace moves

Audit imports and fully qualified names for:

- `Game3D.Keys` to `Input.Key`.
- 3D assets moving to `Graphics3D` ownership.
- unsafe memory/runtime hooks moving out of ordinary docs.
- tooling namespaces moving under a stable tooling boundary or preview docs.

### Security defaults

Audit examples for:

- MD5, SHA1, HMAC-MD5, HMAC-SHA1, and CRC32 outside legacy/checksum examples.
- AES-CBC helpers outside legacy compatibility examples.
- TLS verification bypasses outside explicitly unsafe local-test examples.
- `Math.Random` in secret/token/key/password contexts.

### Stateful result usage

Audit for:

- `LastError`, `Error()`, `LastStatus`, `LastResponse`, `LastOk`.
- `LastFound`, `LastSteps`, `LastHeaderClick`.
- `ResultCount` plus indexed result getters.
- animation event count plus indexed event getters.

Replace with returned result/event objects when those APIs exist.

## Staged Work Plan

### Stage 1: Inventory and denylist

- Generate an old-name and old-contract denylist from the overhaul backlog.
- Search `examples/**`, `docs/**`, `misc/site/**`, `misc/video/**`,
  `tests/runtime/demo_*.zia`, and nested demo directories.
- Classify hits as ordinary usage, migration docs, release notes, historical
  reports, or compatibility tests.

### Stage 2: Focused API examples

- Update `examples/apiaudit/**` first.
- Add focused before/after migration examples only where the old API is likely
  to confuse users.
- Keep BASIC and Zia coverage aligned when both frontends expose the API.

### Stage 3: Small language examples

- Update `examples/zia/**`, `examples/vbasic/**`, and localization examples.
- Keep snippets minimal and easy to read.
- Prefer explicit `Option`/`Result` handling over clever shorthand.

### Stage 4: Runtime demos

- Update `tests/runtime/demo_*.zia`.
- Update `examples/3d/**` starters, probes, and showcases.
- Prioritize input constants, duration units, graphics/asset namespace moves,
  load results, and result/event objects.

### Stage 5: Larger apps and games

- Update `examples/apps/**` and `examples/games/**`.
- Use large examples to validate API ergonomics in real application structure.
- Look for places where the proposed API is too verbose or still ambiguous.
  Feed those findings back into the API plan before finalizing.

### Stage 6: Embedding, graphics C examples, and presentation demos

- Update `examples/embedding/**` if runtime dump or host-facing examples change.
- Update `src/lib/graphics/examples/**` only when lower-level graphics API names
  are affected.
- Update `misc/video/**`, `baseball/demos/**`, and presentation material when
  visible API names or commands change.

### Stage 7: Smoke manifest and verification

Future implementation work should run, at minimum:

```sh
./scripts/example_smoke.sh --audit
./scripts/example_smoke.sh --fast
```

Broad changes should additionally run:

```sh
./scripts/example_smoke.sh --all
ctest --test-dir build -L examples --output-on-failure
./scripts/build_demos_mac.sh
```

Use the appropriate platform build/demo script on Linux and Windows before
landing cross-platform example changes.

## Documentation Coupling

Each example migration should update docs in the same slice:

- namespace page in `docs/viperlib/**`.
- relevant tutorial/reference pages.
- `misc/site/docs/runtime/**` if manual.
- release notes.
- `examples/README.md` or per-example README files when commands or expected
  behavior change.

## Acceptance Criteria

- `examples/apiaudit/**` demonstrates only canonical modern APIs except
  migration-specific files.
- large apps and games do not rely on removed names or sentinel/last-state
  contracts.
- `examples/smoke_manifest.tsv` classifies every example source.
- stale-name scans cover examples, demos, and docs.
- security examples show safe defaults.
- runtime API docs and sample code agree.
