# ADR 0037: Collection Write Verb Aliases

## Status

Accepted

## Context

The runtime overhaul distinguishes collection write verbs from HTTP `PUT`.
Map-like collection writes should use `Set`, while multimap insertion should use
`Add` because it appends another value under the same key.

The old collection names are already public, so the cleanup must be additive.

## Decision

Add canonical aliases:

- `Viper.Collections.LruCache.Set(key, value)`
- `Viper.Collections.BiMap.Set(key, value)`
- `Viper.Collections.MultiMap.Add(key, value)`

Keep compatibility aliases:

- `LruCache.Put`
- `BiMap.Put`
- `MultiMap.Put`

HTTP and REST `Put` APIs keep their protocol verb names.

## Consequences

- Collection docs and examples use verbs that describe the data-structure
  behavior.
- Existing source and IL continue to work.
- Runtime API dumps expose both collection name sets for now.
- Future audits can flag new collection examples that teach `Put` outside
  compatibility coverage while still allowing HTTP `Put`.
