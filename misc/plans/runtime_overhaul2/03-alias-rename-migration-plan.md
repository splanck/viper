# Alias, Rename, And Migration Plan

The current surface bans `RT_ALIAS` but still carries duplicate public
`RT_FUNC` rows that act like aliases. This plan removes that ambiguity.

## Problem

Duplicate public rows create several user-visible problems:

- docs and completion lists show multiple names for one operation;
- migration tooling cannot identify the preferred spelling;
- stability inference can mark both names stable;
- examples can drift between old and new style;
- source review has to inspect C symbols to know whether APIs are distinct.

## Canonicalization Rules

1. Pick one canonical name for every duplicate implementation.
2. Prefer the name that is clearer to a new user.
3. Prefer names that match the global naming rules.
4. If both names are valid but domain-specific, split behavior or document the
   distinction.
5. Mark all non-canonical names `legacy` with `migration_target`.
6. Hide legacy names from default generated docs and default completions.
7. Remove legacy names before 1.0 unless compatibility requirements explicitly
   say otherwise.

## Required Audit

Add an audit that fails on:

- duplicate public C symbol plus identical signature;
- duplicate public canonical leaf with the same owner and behavior;
- duplicate method target exposed by multiple classes without a declared
  facade/adapter annotation;
- duplicate stable row with no migration target;
- duplicate docs anchor.

## Rename Backlog

### P0: Unsafe/Internal Duplicates

| Current names | Decision |
|---|---|
| `Viper.Memory.Retain/Release` and `Viper.Runtime.Unsafe.Retain/Release` | Canonical under `Runtime.Unsafe`; make `Memory` names legacy or remove. |
| `Viper.Error.SetThrowMsg/ClearThrowMsg/SetTrapFields/RaiseKind` and `Runtime.Unsafe.*` | Canonical mutation under `Runtime.Unsafe`; user-facing read-only diagnostics under `Diagnostics`. |
| `Viper.Runtime.GC.*` and `Viper.Runtime.GC.*` | Pick one root. Prefer `Runtime.GC` for runtime machinery; keep `Memory.GC` only if docs position it as user memory tooling. |

### P0: Clear Full-Word Names

| Legacy | Canonical |
|---|---|
| `LeadZ` | `CountLeadingZeros` |
| `TrailZ` | `CountTrailingZeros` |
| `Rotl` | `RotateLeft` |
| `Rotr` | `RotateRight` |
| `Ushr` | `ShiftRightLogical` |
| `Fpr` | `FalsePositiveRate` |
| `NewCap` | `NewCapacity` |
| `SetDTMax` | `SetMaxDeltaTime` |

### P1: Math Short Forms

| Legacy | Canonical |
|---|---|
| `Vec2.Len`, `Vec3.Len` | `Length` |
| `LenSq` | `LengthSquared` |
| `Norm` | `Normalize` |
| `Dist` | `Distance` |
| scalar `Mul` | `Scale` when the operation is scalar scaling |
| `Det` | `Determinant` unless math docs intentionally keep standard shorthand |

Math can keep compact operator-style methods only if the class consistently
uses mathematical operator vocabulary and docs teach it. For broad public API
clarity, full names should be canonical.

### P1: Collection Verb Pairs

| Pair | Decision |
|---|---|
| `LruCache.Put` / `Set` | `Set` canonical. |
| `BiMap.Put` / `Set` | `Set` canonical. |
| `MultiMap.Put` / `Add` | `Add` canonical because it adds another value. |
| `Cap` / `Capacity` | `Capacity` canonical. |

### P1: Formatting And Acronyms

| Legacy | Canonical |
|---|---|
| `Fmt.BoolYN` | `Fmt.YesNo` or `Fmt.BooleanYesNo`; pick one and remove duplicate. |
| `CRC32`, `MD5`, `SHA1`, `SHA256` | Use `Crc32`, `Md5`, `Sha1`, `Sha256` for canonical names, with weak algorithms under `Crypto.Legacy`. |
| `HmacSHA256` | `HmacSha256`. |
| `Pbkdf2SHA256` | `Pbkdf2Sha256`. |

## Migration Implementation Shape

1. Add metadata support for `canonical`, `legacy`, and `migration_target`.
2. Convert duplicate rows to canonical plus legacy metadata.
3. Update generated docs to group legacy names under "Compatibility".
4. Update `viper explain` or a new migration command to report replacements.
5. Update examples and tests to canonical names.
6. Remove or hide legacy rows before release freeze if compatibility permits.

## Compatibility Testing

For every temporary legacy name:

- add a smoke test that canonical and legacy names produce identical behavior;
- add an audit that every legacy name has a migration target;
- add a docs test that examples do not use legacy names;
- add a removal milestone.

