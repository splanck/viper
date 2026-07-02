# Audit, Migration, And Compatibility Plan

## Migration Policy

Viper is still pre-alpha. The default should be direct cleanup rather than
permanent compatibility aliases. Public compatibility aliases may be used only
when they materially reduce migration risk and have a removal date.

Preferred migration order:

1. Add the new canonical API.
2. Update in-repo source, examples, tests, docs, demos, and generated docs.
3. Remove the old public API in the same slice when feasible.
4. If not feasible, hide compatibility behind `RT_INTERNAL_FUNC` or a temporary
   public alias with an explicit removal issue.
5. Add an audit so the old shape cannot reappear.

## ADR Triggers

An implementation slice needs an ADR when it changes:

- runtime C ABI surface,
- public runtime registry policy,
- IL/runtime cross-layer dependency,
- verifier or opcode behavior,
- generated API contracts consumed by tools,
- or normative runtime docs.

Most rename-only public registry changes should still get a compact ADR because
the project operating guide treats runtime C ABI/public runtime surface changes
as ADR-worthy.

## Required Audit Rules

### Signature audit

Fail if public API dump contains:

- `ptr`
- `string` in machine signature text
- malformed generic spelling
- undocumented nullable suffixes such as `str?`
- mismatch between class method/property public signature and global function
  public signature for the same handler

### Naming audit

Fail if public names contain:

- underscore leaves except `get_`/`set_`
- banned abbreviations after their cleanup slice
- `Put` outside HTTP/network allowlist
- new `NewX` named factories outside allowlist
- input constants outside `Viper.Input.Key`

### Failure-shape audit

Classify every public `Try*` function:

- boolean attempt allowlist: `TryEnter`, `TryAcquire`, `TrySend`, etc.
- optional value allowlist: `TryParse`, `TryGet`, `TryRecv`, `TryPop`, etc.
- result value allowlist for diagnostic-producing operations

Fail on `Try*` APIs returning raw `obj`, `str`, or `i64` unless allowlisted with
a documented reason.

### Property audit

Fail on simple `Set<Property>` methods when the class also exposes `Property`,
unless the pair is allowlisted as a command-style mutation.

### Namespace audit

Fail on:

- `Viper.Game3D.Keys`
- new key constants outside input namespace
- 3D render/asset primitives under `Viper.Game3D`
- game controllers under `Viper.Graphics3D`
- unsafe memory hooks outside unsafe/internal namespace
- trap-state mutation hooks outside unsafe/internal namespace
- orphan tooling namespaces without stability/capability metadata

### Large class audit

Warn or fail when a class exceeds 50 public members unless allowlisted with a
reason.

### Contract metadata audit

Warn or fail when public APIs lack required metadata after the relevant slice:

- timeout and duration parameters without units.
- raw integer enum domains without a declared domain.
- fallible APIs without fallibility classification.
- object-returning APIs without ownership/lifetime classification.
- callback-taking APIs without invocation-thread and lifetime notes.
- public rows without stability, capability, and docs-anchor fields.

### Security audit

Fail ordinary public docs and new examples when they recommend:

- MD5, SHA1, CRC32, or HMAC-MD5/HMAC-SHA1 for new security-sensitive code.
- legacy AES-CBC helpers outside legacy/migration sections.
- disabling TLS verification except in an explicitly labeled unsafe test setup.
- crypto decryption APIs that return `NULL` without a result/error channel.

### Side-channel result audit

Warn or fail on production APIs where the only error/status path is:

- `Error()`
- `LastError`
- `LastStatus`
- `LastResponse`
- `LastOk`
- `LastFound`
- `LastSteps`
- `ResultCount` plus indexed `GetResult`

## Snapshot Testing

Add a checked-in generated summary fixture, not necessarily the full 6,000-line
dump. Useful fixture fields:

- total function/class/property/method counts
- namespace prefix counts
- list of public `Try*` functions and return shapes
- list of large classes
- list of banned-name hits
- list of unsafe/internal public names

The full dump can remain generated in build artifacts if it is too noisy for
source control.

## Verification Commands

For implementation slices:

```sh
VIPER_SKIP_CLEAN=1 VIPER_CMAKE_GENERATOR=Ninja ./scripts/build_viper_mac.sh
./scripts/audit_runtime_surface.sh
./scripts/check_runtime_completeness.sh
bash scripts/lint_zia_runtime_names.sh
```

When names affect examples/docs:

```sh
./scripts/build_demos_mac.sh
ctest --test-dir build --output-on-failure
```

Example-only migration slices should also use:

```sh
./scripts/example_smoke.sh --audit
./scripts/example_smoke.sh --fast
./scripts/example_smoke.sh --all
ctest --test-dir build -L examples --output-on-failure
```

The current planning pass is document-only and intentionally does not run these
commands.

For cross-platform-sensitive slices:

```sh
./scripts/lint_platform_policy.sh
```

Use the Linux/Windows build scripts on those platforms before landing broad
runtime changes.

## Documentation Migration

Every API cleanup slice must update:

- `docs/viperlib/**` page for that namespace.
- relevant tutorial/example snippets.
- `examples/smoke_manifest.tsv` when classifications change.
- `examples/apiaudit/**`, larger apps/games, and `tests/runtime/demo_*.zia`
  when public names or semantics change.
- `misc/site/docs/runtime/**` if manually maintained.
- `docs/zia-reference.md` or BASIC docs if syntax examples change.
- generated API snapshots if introduced.

Docs should present properties as properties and avoid teaching generated
accessor spellings unless required by a language frontend.

## Suggested Release Notes Template

```markdown
### Runtime API cleanup

- Renamed `Old.Name` to `New.Name`.
- Replaced sentinel failure return with `Option`/`Result`.
- Removed duplicate surface `Duplicate.Name`; use `Canonical.Name`.
- Updated docs and examples.
```

Do not add AI attribution or generated-by footers.

## Long-Term Completion Criteria

The overhaul is complete when:

- public API dump has one signature dialect,
- all policy audits pass,
- runtime docs use the canonical names,
- no duplicated key/asset/namespace concepts remain,
- public failure contracts avoid ambiguous sentinels,
- unsafe internals are hidden or clearly unsafe,
- disabled capability behavior is consistent and tested,
- large classes are either split or deliberately allowlisted,
- examples and demos are audited, migrated, and covered by the smoke manifest,
- and full build/test lanes pass locally.
