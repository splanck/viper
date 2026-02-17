# Viper Runtime Library Reference

> **This documentation has moved to [docs/viperlib/](viperlib/README.md)**

The Viper Runtime Library documentation has been reorganized into separate files for easier navigation:

| Module                                   | Description                                                               |
|------------------------------------------|---------------------------------------------------------------------------|
| [Architecture](viperlib/architecture.md) | Runtime internals, type reference                                         |
| [Audio](viperlib/audio.md)               | `Audio`, `Music`, `Sound`, `Voice` — audio playback for games and applications |
| [Collections](viperlib/collections.md)   | `Bag`, `Bytes`, `Deque`, `Heap`, `LazySeq`, `List`, `Map`, `Queue`, `Ring`, `Seq`, `Set`, `SortedSet`, `Stack`, `TreeMap`, `WeakMap` |
| [Core Types](viperlib/core.md)           | `Box`, `MessageBus`, `Object`, `String` — foundational types              |
| [Cryptography](viperlib/crypto.md)       | `Aes`, `Cipher`, `Hash`, `KeyDerive`, `Password`, `Rand`, `Tls`           |
| [Diagnostics](viperlib/diagnostics.md)   | `Assert`, `Trap` — assertion checking and traps                           |
| [Functional](viperlib/functional.md)     | `Lazy`, `Option`, `Result` — lazy evaluation, optionals, and result types  |
| [Game Utilities](viperlib/game.md)       | `ButtonGroup`, `Collision`, `CollisionRect`, `Grid2D`, `ObjectPool`, `ParticleEmitter`, `PathFollower`, `Physics2D`, `Quadtree`, `ScreenFX`, `SmoothValue`, `SpriteAnimation`, `SpriteSheet`, `StateMachine`, `Timer`, `Tween` |
| [Graphics](viperlib/graphics.md)         | `Camera`, `Canvas`, `Color`, `Pixels`, `Scene`, `SceneNode`, `Sprite`, `SpriteBatch`, `Tilemap` |
| [GUI](viperlib/gui.md)                   | `App`, `Button`, `Label`, widgets — GUI toolkit for applications          |
| [Index](viperlib/README.md)              | Overview and quick navigation                                             |
| [Input](viperlib/input.md)               | `Action`, `Keyboard`, `KeyChord`, `Manager`, `Mouse`, `Pad` — input for games and interactive apps |
| [Input/Output](viperlib/io.md)           | `Archive`, `BinaryBuffer`, `BinFile`, `Compress`, `Dir`, `File`, `Glob`, `LineReader`, `LineWriter`, `MemStream`, `Path`, `Stream`, `TempFile`, `Watcher` |
| [Mathematics](viperlib/math.md)          | `Bits`, `Easing`, `Math`, `PerlinNoise`, `Quaternion`, `Random`, `Spline`, `Vec2`, `Vec3` |
| [Network](viperlib/network.md)           | `Dns`, `Http`, `HttpReq`, `HttpRes`, `RateLimiter`, `RestClient`, `RetryPolicy`, `Tcp`, `TcpServer`, `Udp`, `Url`, `WebSocket` |
| [System](viperlib/system.md)             | `Environment`, `Exec`, `Machine`, `Terminal`                              |
| [Text Processing](viperlib/text.md)      | `Codec`, `CompiledPattern`, `Csv`, `Diff`, `Html`, `Ini`, `Json`, `JsonPath`, `JsonStream`, `Markdown`, `NumberFormat`, `Pattern`, `Pluralize`, `Scanner`, `StringBuilder`, `Template`, `TextWrapper`, `Toml`, `Uuid`, `Version` |
| [Threads](viperlib/threads.md)           | `Async`, `Barrier`, `CancelToken`, `ConcurrentMap`, `Debouncer`, `Future`, `Gate`, `Monitor`, `Parallel`, `Pool`, `Promise`, `RwLock`, `SafeI64`, `Scheduler`, `Thread`, `Throttler` |
| [Time & Timing](viperlib/time.md)        | `Clock`, `Countdown`, `DateOnly`, `DateRange`, `DateTime`, `Duration`, `RelativeTime`, `Stopwatch` |
| [Utilities](viperlib/utilities.md)       | `Convert`, `Fmt`, `Log`, `Parse`                                          |
