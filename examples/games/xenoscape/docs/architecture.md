# Developer architecture

`game.zia` owns the application state machine and long-lived systems. Persistent
authority is deliberately narrow:

- `AbilityManager` owns the six-bit ability mask.
- `WorldState` owns stable interaction and room-discovery masks.
- `SaveManager` owns schema validation, three slots, migration, and backup.
- `WorldGraph` owns ten nodes, twelve bidirectional gated edges, and room names.
- `MetaProgression` owns best regional results and New Game+ state.
- `GameSettings` owns global normalized settings.

`Level` builds the active regional tilemap, spawn tables, interaction records,
content anchors, dynamic destructible/crumble state, and regional mechanics.
`EnemyManager`, `ProjectilePool`, `PickupManager`, and `ParticleSystem` use fixed
pools or retained lists. `Renderer`, `GameCamera`, `HUD`, and `MenuSystem` own
presentation; gameplay never depends on authored bitmap availability.

Simulation uses integer centipixels and millisecond timers. The main Canvas
clamps frame delta. Game Speed scales live gameplay only; hit stop and rumble use
the unscaled frame delta. Collision is axis-separated with a six-pixel upward
corner correction and an 80 ms wall-stick grace. Camera framing has bounded
velocity look-ahead and delayed vertical intent.

The runtime JSON contains release-on migration toggles for world graph, authored
art, and adaptive music. Procedural art/audio are permanent fallbacks and are
tested directly, not deleted legacy paths.
