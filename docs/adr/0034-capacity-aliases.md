# ADR 0034: Capacity Aliases

## Status

Accepted

## Context

Several collection and thread APIs exposed capacity as `Cap`, and
`Viper.IO.BinaryBuffer` exposed `NewCap`. These abbreviations are short but not
as readable as the rest of the public runtime surface. Other runtime classes
already use `Capacity`, including `MemStream`, `StringBuilder`, `SpriteBatch`,
and `ObjectPool`.

Existing source and generated IL use the old names, so the cleanup must remain
additive.

## Decision

Add canonical aliases:

- `Viper.Collections.LruCache.Capacity`
- `Viper.Collections.Ring.Capacity`
- `Viper.Collections.Seq.Capacity`
- `Viper.Collections.Deque.Capacity`
- `Viper.Threads.Channel.Capacity`
- `Viper.IO.BinaryBuffer.NewCapacity(capacity)`

Keep compatibility aliases:

- `Cap` on the affected collection/channel classes
- `Viper.IO.BinaryBuffer.NewCap`

All aliases lower to the same runtime C implementations.

## Consequences

- Public docs and API audit examples can consistently teach `Capacity`.
- Existing source and IL continue to work.
- Runtime API dumps expose both name sets for now.
- Future API audits should flag new user-facing examples that prefer `Cap` or
  `NewCap` outside compatibility coverage.
