---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0035: BloomFilter False Positive Rate Alias

## Status

Accepted

## Context

`Viper.Collections.BloomFilter.Fpr()` exposes the estimated false positive rate
using an abbreviation that is familiar to some probabilistic-data-structure
users but not clear as a public method name. The runtime overhaul naming policy
expands ambiguous abbreviations in user-facing leaves.

Existing code may already call `Fpr()`, so the change must be additive.

## Decision

Add `Viper.Collections.BloomFilter.FalsePositiveRate() -> f64` as the canonical
public method.

Keep `Viper.Collections.BloomFilter.Fpr()` as a compatibility alias. Both
methods call the same runtime implementation.

## Consequences

- Docs and examples can describe the metric directly in the method name.
- Existing source and IL remain compatible.
- Runtime API dumps expose both names until a future compatibility cleanup.
- API audits should prefer `FalsePositiveRate()` except in compatibility
  examples.
