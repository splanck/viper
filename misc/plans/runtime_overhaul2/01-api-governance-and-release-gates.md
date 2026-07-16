# API Governance And Release Gates

This document defines how the public runtime API should be stabilized. These
rules apply to all `Viper.*` rows emitted by `viper --dump-runtime-api`.

## Public Surface Definition

The public API is:

- every public `RT_FUNC` canonical name beginning with `Viper.`;
- every emitted runtime class;
- every emitted class method;
- every emitted class property;
- every generated migration target, docs anchor, stability marker,
  capability marker, fallibility marker, ownership marker, and type signature.

Runtime C symbols are not user-facing names, but duplicate C symbols are still
part of the public governance problem because they reveal duplicate API rows.

## Stability Tiers

Use four stability tiers:

| Tier | Meaning |
|---|---|
| `stable` | Release-supported, documented, audited, and migration-protected. |
| `preview` | Public but still allowed to change before a stable release. |
| `legacy` | Public only for migration; hidden from beginner docs and examples. |
| `unsafe` | Public but explicitly low-level, security-sensitive, or runtime-internal-adjacent. |

Decision: no row should become `stable` by inference. Stability must be
declared in `runtime.def` metadata or a generated sidecar owned by `rtgen`.

## ADR Triggers

Per repository policy, implementation requires ADRs for:

- runtime C ABI surface changes;
- IL opcode, grammar, verifier-rule, or cross-layer dependency changes;
- `docs/il/il-guide.md#reference` changes;
- workflow changes under `.github/workflows/*`.

Runtime API rename work often touches the C ABI and generated registry, so plan
ADR coverage before starting large mechanical changes.

## Release Gates

The API is not ready to freeze until these gates pass:

1. No duplicate public C-symbol/signature groups except explicitly approved
   compatibility shims hidden from normal discovery.
2. No public bare `obj` where a concrete `obj<T>` can be known.
3. No public `ownership: unknown` on rows returning or storing object handles.
4. No unresolved public docs anchors.
5. No side-channel diagnostic API marked `stable`.
6. No sentinel API marked `stable` unless the sentinel is the domain value and
   the docs explicitly define it.
7. No capability-dependent stable API without declared availability behavior.
8. No public stub that silently returns a plausible success value.
9. No same class/name/arity method collision.
10. No boolean setter/getter type mismatch.
11. No public non-PascalCase leaf except accessor prefixes `get_` and `set_`.
12. No high-arity public API above the approved threshold without a waiver.
13. All examples, demos, and docs use canonical names.
14. `rtgen --audit` and the new runtime API audit suite pass locally.

## Compatibility Policy

Because this work is pre-release stabilization, prefer removing bad public
names over carrying aliases indefinitely. If compatibility is still needed:

- keep one canonical stable name;
- mark old names `legacy`;
- provide an exact migration target;
- hide legacy names from default docs and completions;
- add tests that old and new names have identical behavior until removal.

Do not add new `RT_ALIAS`. Do not add duplicate `RT_FUNC` rows as a workaround.
If an implementation needs two public names temporarily, the compatibility path
must be explicit in metadata and audited.

## Waivers

Some APIs will need temporary waivers. A waiver must include:

- affected public row;
- reason the row cannot be fixed now;
- user-visible risk;
- planned replacement;
- owner or workstream;
- removal date or release milestone.

Waivers belong in generated audit fixtures or a checked-in waiver file, not in
comments scattered across implementation files.

