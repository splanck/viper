# Documentation Update Plan

## Goal

Runtime API cleanup is not complete until the docs, examples, and published
reference material teach the new API instead of the old one. Documentation work
must ship in the same implementation slices as API changes, not as a cleanup
afterward.

## Documentation Source Map

Update these areas when public runtime APIs change:

| Area | Files |
|---|---|
| Runtime library reference | `docs/viperlib/**` |
| Runtime overview | `docs/viperlib/README.md`, `docs/viperlib/architecture.md` |
| Zia examples and syntax references | `docs/zia-reference.md`, `docs/zia-getting-started.md`, `docs/frontend-howto.md` |
| BASIC examples where affected | `docs/basic-reference.md`, `docs/basic-language.md`, `docs/basic-namespaces.md` |
| Book/tutorial material | `docs/bible/**` |
| Tooling/API discovery docs | `docs/tools.md`, `docs/debugging.md`, `docs/runtime_class_howto.md`, `docs/runtime_extend_howto.md` |
| Site runtime pages | `misc/site/docs/runtime/**` |
| Examples and demos | `examples/**`, `misc/video/**`, demo plans under `misc/plans/**` when referenced |
| Runtime demo fixtures | `tests/runtime/demo_*.zia` |
| External or nested demos | `baseball/demos/**`, `baseball/src/demo/**` |
| Embedding and C examples | `examples/embedding/**`, `src/lib/graphics/examples/**` |
| Release notes | `docs/release_notes/Viper_Release_Notes_0_2_99.md` or the active release note file |

If any of these areas are generated from another source, update the generator or
source material instead of hand-editing generated output.

## Per-Slice Documentation Checklist

Every API cleanup slice should include:

- User-facing explanation of the new canonical name.
- Updated API tables.
- Updated code snippets in Zia and BASIC when the API is available to both.
- Removal of old names from ordinary examples.
- A migration note mapping old names to new names.
- Updated failure semantics if return types change.
- Updated capability/stub behavior notes if construction or availability
  changes.
- Release note entry under a runtime API cleanup heading.

## Naming Cleanup Docs

When renaming APIs:

1. Update the namespace page in `docs/viperlib`.
2. Search docs for the old public name.
3. Replace ordinary usage with the new name.
4. Leave the old name only in migration notes or release notes.
5. Add an audit denylist entry after old-name references are gone.

Examples:

- `LeadZ` should disappear from normal math docs after it becomes
  `CountLeadingZeros`.
- `Cap` should disappear from collection docs after it becomes `Capacity`.
- `SetDTMax` should disappear from canvas docs after it becomes
  `SetMaxDeltaTime`.

## Failure Semantics Docs

When moving from sentinels to `Option` or `Result`, docs must explain:

- what failure means,
- how to inspect the return value,
- which API traps on failure,
- which API returns failure as data,
- and whether ownership/lifetime of returned objects changes.

Old wording to eliminate from normal public docs after migration:

- "returns null on failure"
- "returns 0 on failure"
- "returns empty string when missing"
- "call Error() for details"
- "check LastError"
- "check LastStatus" as the only way to learn request outcome

Those phrases may remain only in historical migration notes.

## Security Documentation

Crypto and networking docs should be safe by default:

- MD5, SHA1, CRC32, HMAC-MD5, and HMAC-SHA1 appear only in legacy,
  compatibility, checksum, or migration sections.
- AES-CBC helpers are documented as legacy if authenticated encryption is the
  recommended modern path.
- TLS verification bypasses are shown only in explicitly unsafe local-test
  examples.
- `Math.Random` docs say it is deterministic/non-cryptographic; `Crypto.Rand`
  owns security-sensitive randomness.
- Decryption and certificate failures use `Result` examples after the API is
  migrated.

## Namespace Cleanup Docs

Namespace docs should teach ownership explicitly:

- `Viper.Input.Key` owns key constants.
- `Viper.Input.Keyboard` owns keyboard state.
- `Viper.Graphics` owns immediate/shared graphics.
- `Viper.Graphics2D` owns retained 2D systems.
- `Viper.Graphics3D` owns retained 3D rendering/assets/physics.
- `Viper.Game3D` owns high-level gameplay world/entity/controller conveniences.

Each affected page should include a short "Use this when..." paragraph near the
top so users do not need to infer ownership from examples.

## Generated Reference Strategy

The public API dump should become the source for generated reference checks.
Recommended generated documentation flow:

1. Generate the runtime API JSON from the built tool during docs generation.
2. Render compact API tables from the JSON.
3. Keep conceptual docs handwritten.
4. Fail docs generation if handwritten API tables mention names missing from the
   public dump.

If full generation is too large for the first slice, add a lightweight stale-doc
scanner that checks old-name denylist entries against `docs/**` and `misc/site/**`.

## Site Update Strategy

The static site under `misc/site/docs/runtime/**` appears to carry runtime API
pages separate from `docs/viperlib/**`. For each runtime API slice:

- identify whether the site page is generated or manual,
- update it in the same change if manual,
- otherwise update the source generator,
- and add old-name denylist coverage after the site is clean.

Do not let the site continue teaching legacy names after the repo docs move.

## Examples And Demos

Examples are documentation. For every API cleanup:

- update source examples,
- update demo scripts,
- update tutorial snippets,
- update screenshots/video scripts only when names are visible,
- and keep old names out of new sample code.

For large API changes, add a small migration example showing before/after usage.

Concrete sample surfaces to audit:

- `examples/apiaudit/**` for narrow API coverage.
- `examples/3d/**` and `tests/runtime/demo_*.zia` for 3D and rendering
  migration.
- `examples/apps/**` and `examples/games/**` for real-application ergonomics.
- `examples/vbasic/**`, `examples/sqldb-basic/**`, and BASIC game ports for
  BASIC-facing runtime names.
- `examples/localization/**` for locale parse/load and message lookup behavior.
- `examples/embedding/**` and `src/lib/graphics/examples/**` for host-facing
  API examples.
- `misc/video/**`, `baseball/demos/**`, and `misc/plans/demos/**` for demo
  scripts and presentation material.

## Release Notes

Use a consistent release-note block:

```markdown
### Runtime API cleanup

- Renamed `Old.Name` to `New.Name`.
- Replaced `Old.TryThing()` sentinel failure with `Option`/`Result`.
- Removed duplicate `Old.Namespace.Name`; use `Canonical.Namespace.Name`.
- Updated docs, examples, and site reference pages.
```

The release note should not be the only place users learn the new API. It is an
index of changes, not the canonical reference.

## Stale Documentation Audit Plan

After each rename category is complete, add old names to a stale-doc denylist.
The denylist should scan:

- `docs/**`
- `misc/site/docs/**`
- `examples/**`
- `misc/video/**`
- `tests/runtime/demo_*.zia`
- `baseball/demos/**`
- `baseball/src/demo/**`
- relevant `misc/plans/**` files except migration history

Allow old names only in:

- migration sections,
- release notes,
- historical reports,
- and explicit compatibility tests.

## Completion Criteria

Documentation is complete when:

- each changed API has updated reference docs,
- examples compile conceptually against canonical names,
- old names appear only in migration/history contexts,
- release notes list the breaking changes,
- site runtime pages match repo docs,
- and stale-doc audits cover cleaned-up names.
