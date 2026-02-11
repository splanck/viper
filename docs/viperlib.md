# Viper Runtime Library Reference

> **This documentation has moved to [docs/viperlib/](viperlib/README.md)**

The Viper Runtime Library documentation has been reorganized into separate files for easier navigation:

| Module                                   | Description                                                               |
|------------------------------------------|---------------------------------------------------------------------------|
| [Architecture](viperlib/architecture.md) | Runtime internals, type reference                                         |
| [Audio](viperlib/audio.md)               | `Sound`, `Music`, `Voice`, `Audio` — audio playback for games and applications |
| [Collections](viperlib/collections.md)   | `Bag`, `Bytes`, `Deque`, `Heap`, `LazySeq`, `List`, `Map`, `Queue`, `Ring`, `Seq`, `Set`, `SortedSet`, `Stack`, `TreeMap`, `WeakMap` |
| [Core Types](viperlib/core.md)           | `Object`, `Box`, `String` — foundational types                            |
| [Cryptography](viperlib/crypto.md)       | `Hash`, `KeyDerive`, `Rand`, `Tls`                                        |
| [Diagnostics](viperlib/diagnostics.md)   | `Assert`, `Trap` — assertion checking and traps                           |
| [Game Utilities](viperlib/game.md)       | `Grid2D`, `Timer`, `StateMachine`, `Tween`, `ObjectPool`, `Quadtree`, `Physics2D`, `SpriteSheet`, `ParticleEmitter`, `SpriteAnimation`, `CollisionRect`, `Collision`, `ButtonGroup`, `SmoothValue`, `ScreenFX`, `PathFollower` |
| [Graphics](viperlib/graphics.md)         | `Canvas`, `Color`, `Pixels`, `Sprite`, `Tilemap`, `Camera`, `SceneNode`, `Scene`, `SpriteBatch` |
| [GUI](viperlib/gui.md)                   | `App`, `Button`, `Label`, widgets — GUI toolkit for applications          |
| [Index](viperlib/README.md)              | Overview and quick navigation                                             |
| [Input](viperlib/input.md)               | `Action`, `Keyboard`, `KeyChord`, `Manager`, `Mouse`, `Pad` — input for games and interactive apps |
| [Input/Output](viperlib/io.md)           | `Archive`, `BinFile`, `Compress`, `Dir`, `File`, `LineReader`, `LineWriter`, `MemStream`, `Path`, `Watcher` |
| [Mathematics](viperlib/math.md)          | `Bits`, `Math`, `Quaternion`, `Random`, `Spline`, `Vec2`, `Vec3`          |
| [Network](viperlib/network.md)           | `Dns`, `Http`, `HttpReq`, `HttpRes`, `RateLimiter`, `RestClient`, `RetryPolicy`, `Tcp`, `TcpServer`, `Udp`, `Url`, `WebSocket` |
| [System](viperlib/system.md)             | `Environment`, `Exec`, `Machine`, `Terminal`                              |
| [Text Processing](viperlib/text.md)      | `Codec`, `Csv`, `Html`, `Json`, `JsonPath`, `JsonStream`, `Markdown`, `Pattern`, `Serialize`, `StringBuilder`, `Template`, `Toml`, `Uuid` |
| [Threads](viperlib/threads.md)           | `Async`, `Barrier`, `CancelToken`, `ConcurrentMap`, `Debouncer`, `Gate`, `Monitor`, `Parallel`, `Pool`, `Promise`, `Future`, `RwLock`, `SafeI64`, `Scheduler`, `Thread`, `Throttler` |
| [Time & Timing](viperlib/time.md)        | `Clock`, `Countdown`, `DateTime`, `Stopwatch`                             |
| [Utilities](viperlib/utilities.md)       | `Convert`, `Fmt`, `Log`                                                   |
