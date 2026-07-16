---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0054: UnionFind Root Lookup Option API

Date: 2026-07-02
Status: Accepted

## Context

`Viper.Collections.UnionFind.Find(x)` is the conventional disjoint-set "find"
operation, but it returns `-1` when `x` is outside the structure. That sentinel
is safe for compatibility, but it forces callers to remember a magic value and
makes the method harder to read for users who are not already familiar with the
union-find algorithm.

## Decision

Add `Viper.Collections.UnionFind.FindRootOption(x)`.

The new API returns `Some(root)` when `x` is a valid element and `None` when the
element is invalid. The name uses "Root" to make the result clear: the method
returns the representative/root element for the set containing `x`, not an
arbitrary search result.

`Find(x)` remains available for compatibility and keeps its existing `-1`
sentinel behavior. Runtime API metadata marks `Find` as legacy with
`FindRootOption` as its migration target.

## Consequences

- New code can branch on `Option` instead of comparing against `-1`.
- Existing code using the standard union-find `Find` name is preserved.
- Documentation and API audit examples must show `FindRootOption` as the
  preferred lookup API and keep `Find` documented as compatibility behavior.
