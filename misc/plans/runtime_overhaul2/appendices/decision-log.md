# Decision Log

This is the compact proposed decision list for implementation planning.

| ID | Decision |
|---|---|
| D001 | One canonical public name per concept; aliases become legacy or hidden compatibility rows. |
| D002 | Canonical names use PascalCase full words; compressed abbreviations are legacy. |
| D003 | Canonical acronym style is word-cased for user-facing names: `Json`, `Http`, `Tls`, `Sha256`, `HmacSha256`. |
| D004 | Public concrete handles use `obj<T>`; bare `obj` is reserved for intentionally dynamic values. |
| D005 | Stable object/string rows must declare ownership; `unknown` is not release-ready. |
| D006 | `Result<T>` is canonical for operation failure. |
| D007 | `Option<T>` is canonical for ordinary absence. |
| D008 | Side-channel error APIs are legacy unless scoped as diagnostic telemetry. |
| D009 | Sentinel APIs are legacy unless the sentinel is the domain value and explicitly documented. |
| D010 | Trap-state mutation belongs under `Runtime.Unsafe` or internal APIs, not ordinary `Viper.Error`. |
| D011 | Disabled capabilities must return unavailable results or trap; stable APIs cannot silently no-op or return fake success values. |
| D012 | Runtime catalog metadata must be declared, not inferred by CLI name heuristics. |
| D013 | Docs anchors must resolve to generated or handwritten reference entries. |
| D014 | High-arity APIs require value-object/config alternatives or waivers. |
| D015 | Numeric time, coordinate, angle, color, and timeout parameters need unit metadata. |
| D016 | Integer constants need enum/domain metadata. |
| D017 | `Input.Key` is canonical; `Input.Keyboard.Key*` and `Game3D.Keys` are legacy. |
| D018 | `Text` owns text algorithms; `Data` owns structured data formats. |
| D019 | `Graphics2D` and `Graphics3D` own dimension-specific rendering; `Graphics` owns shared primitives and compatibility. |
| D020 | `Game`, `Game2D`, and `Game3D` separate engine-generic and dimension-specific gameplay systems. |
| D021 | Canonical audio root should be `Audio`; `Sound` becomes asset terminology or legacy namespace. |
| D022 | Properties represent state; methods represent work. Mutable state should not appear as read-only property plus setter unless compatibility requires it. |
| D023 | Constructor auto-injection must be explicit in metadata or source. |
| D024 | Runtime method lookup must audit same class/name/arity collisions. |
| D025 | Examples and demos are migration clients and must use canonical APIs. |

