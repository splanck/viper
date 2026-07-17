# XENOSCAPE Commercial-Readiness Program

Status: project-scope implementation complete; external platform certification pending.

Version: `0.3.0` development preview.

1.0 status: retracted; no 1.0 claim is authorized by this document.
Scope: `examples/games/xenoscape/` and its registered project tests

## Objective and release rule

The program converts the overhauled demo from an obstructed, internally
inconsistent showcase into a playable, testable action-Metroidvania candidate.
It specifically addresses disabled required movement, overlapping UI, unclear
progression, generic region structure, incomplete combat/presentation, fragile
saves, and unsupported release claims.

Implementation completion is not the same as commercial certification.
XENOSCAPE stays below 1.0 until hands-on keyboard/gamepad playthroughs and
package/install checks pass on every intended macOS, Windows, and Linux target.
One host's deterministic probes may establish code readiness but may not be
rewritten as multi-platform evidence.

## Implemented upgrade ledger

| # | Upgrade | Implemented outcome | Primary evidence |
|---:|---|---|---|
| 1 | Retract false 1.0 | Metadata and docs say 0.3.0 development preview | `zanna.project`, README, development notes |
| 2 | Restore Double Jump | Wall Jump + Double Jump are baseline in new, default, and migrated profiles | `abilities.zia`, `player.zia`, `save.zia` |
| 3 | Physics-aware traversal validation | Stand-cell flood uses shipped movement/ability envelopes | `level_validation.zia` |
| 4 | Real vertical-slice gate | Crash/Fungal reachability, gateways, first reward, and next gate are proved | `playthrough_probe.zia` |
| 5 | Overlay coordinator | One modal, top, and lower-third owner per frame | `ui_coordinator.zia`, `game.zia` |
| 6 | Dismissible guidance | Tutorials and ability cards expire, dismiss, acknowledge, and replay | `tutorials.zia`, `abilities.zia`, `ui_flow_probe.zia` |
| 7 | Unified ability ownership | AbilityManager alone owns grants and acquisition cards | `abilities.zia`, `game.zia` |
| 8 | HUD redesign | Stable base strip, optional secondary detail, Full-only minimap | `hud.zia`, `settings.zia` |
| 9 | Text/modal UX | Wrapped scalable text and corrected scale/color call order | `menu.zia`, `lore.zia`, `radio.zia`, `profile_flow.zia` |
| 10 | Authored rooms | Eight named nonuniform rooms per region | `world_graph.zia`, `campaign_content.zia` |
| 11 | Re-author blocked geometry | Six validator-found route blockers repaired | `level.zia`, `level_validation_probe.zia` |
| 12 | Physical world graph | Every edge has endpoint rooms and reciprocal in-level gateways | `world_graph.zia`, `level.zia`, `game.zia` |
| 13 | Ability curriculum | Baseline movement -> Dash -> Charge -> Pound -> Grapple | `campaign_content.zia`, `world_probe.zia` |
| 14 | Interaction discoverability | Nearest authored action gets a ring and live-binding prompt | `game.zia`, `level.zia` |
| 15 | Recovery tools | No-penalty Return to Checkpoint plus ordinary death recovery | `menu.zia`, `game.zia` |
| 16 | Combat polish | Correct stomp direction, hit feedback, immunity feedback, phase cues | `game.zia`, `enemy.zia`, `hud.zia` |
| 17 | Enemy/boss production | Ten named climaxes and six new regional signatures | `enemy.zia`, `level.zia`, `radio.zia`, `lore.zia` |
| 18 | Progression clarity | Salvage is spendable; coins/score are arcade-only; objectives defer to threats | `hud.zia`, `menu.zia`, `objectives.zia` |
| 19 | Camera framing | Boss focus window with deterministic release paths | `camera.zia`, `game.zia` |
| 20 | Coherent art/VFX | New original pixel title/ARIA art, boss symbols, retained fallbacks | `assets/`, `enemy.zia`, `assets/README.md` |
| 21 | Audio/narrative pacing | Bounded intro/radio, removed duplicate splash, narrative ducking | `game.zia`, `radio.zia`, `sound.zia` |
| 22 | Accessibility/input parity | Twenty settings, HUD/aim choices, map/text scaling, named pad bindings | `settings.zia`, `worldmap.zia`, `game.zia` |
| 23 | Save hardening | Schema-v4 checksum, full staging, migration, selective recovery, backup | `save.zia`, `progression_probe.zia` |
| 24 | Release QA matrix | Playthrough/UI/cadence/render/reachability/performance/soak gates | project probes, `src/tests/CMakeLists.txt` |
| 25 | Reproducible packaging | Truthful versioned tarball plan, inventory, extracted smoke mode | `zanna.project`, `main.zia`, `package_probe.zia` |

The detailed player-facing change report is
`examples/games/xenoscape/docs/release-notes.md`.

## Authoritative gameplay contracts

### Ability curriculum

| Ability | Flag | Acquisition | Campaign role |
|---|---:|---|---|
| Wall Jump | 1 | Baseline suit | Vertical wall routes and recovery |
| Double Jump | 2 | Baseline suit | Required reach for all starting geometry |
| Dash | 4 | Spore Mother | Opens Fungal-to-Crystal progression |
| Charge Shot | 8 | Crystal Wyrm or Lake shrine | Reinforced walls and charged combat |
| Ground Pound | 16 | Magma Core | Breakable floors and radial stagger |
| Grapple | 32 | Overgrowth climax/shrine | Anchor traversal and final Core route |

AbilityManager is authoritative. Player receives a synchronized mask for
simulation; SaveManager serializes that same mask. Re-granting is idempotent.

### World and room graph

The graph has ten regions, twelve bidirectional edges, and eight stable named
rooms per region. Every edge records its two endpoint rooms. Level construction
places an authored `INTERACTION_REGION_GATE` at each incident endpoint with the
same edge id, ability requirement, and destination. Entering a region positions
the player at the reciprocal gateway.

The ten climaxes are Beacon Warden, Spore Mother, Crystal Wyrm, Relay Custodian,
Tide Leviathan, Magma Core, Verdant Colossus, Cryo Custodian, Null Harvester,
and the Architect.

### Presentation ownership

Priority is deterministic:

- Modal: Map -> Lore -> Ability -> Transition.
- Top: Boss -> Interaction message -> Room -> Achievement.
- Bottom: Tutorial -> ARIA radio.

Any modal suppresses both nonmodal lanes. Tutorial cards last at most 7,000 ms;
ability cards last at most 1,800 ms. Both allow explicit dismissal, and
tutorials can be replayed from player guidance.

### Save integrity

Schema 4 stores three slots. Each used slot carries a rolling checksum over its
core progression, world masks, room masks, times, ranks, tutorials, checkpoint,
difficulty, NG+ flag, and write stamp. Older valid schemas migrate to schema 4
and receive baseline Double Jump. A checksum mismatch or malformed store is
backed up before the damaged slot/store is made unavailable.

## Verification record

The following project-scoped evidence passed on 2026-07-10 using the existing
Zanna binary, without rebuilding Zanna and without running Zanna ctests:

- whole-directory JSON diagnostic check;
- mechanics, world, meta, settings, campaign, progression, action-name, UI-flow,
  cadence, performance, and 3,600-frame soak probes;
- focused two-level playthrough proof;
- ten of ten production levels reachable at their exits, checkpoints, bosses,
  and named rooms;
- Canvas smoke and expanded authored/fallback/large-text render probes;
- asset/package inventory probe;
- 0.3.0 macOS arm64 tarball creation, archive inventory, extraction, and
  `RESULT: package_smoke_ok` from the packaged executable.

The direct commands and CTest registrations are documented in
`examples/games/xenoscape/docs/internals/testing.md`.

## 1.0 authorization checklist

These are release-management gates, not unfinished game-code claims:

- [ ] Complete exploratory campaign playthrough with keyboard.
- [ ] Complete exploratory campaign playthrough with a supported gamepad.
- [ ] Run the root platform build and Xenoscape lanes on macOS.
- [ ] Run the root platform build and Xenoscape lanes on Windows.
- [ ] Run the root platform build and Xenoscape lanes on Linux.
- [ ] Exercise install, launch, save/write permissions, update, and uninstall for
      every distribution format intended to ship.
- [ ] Complete signing/notarization and legal/store review where applicable.
- [ ] Resolve all resulting blockers and repeat the full matrix on the exact
      candidate artifacts.

Until those boxes are supported by dated evidence, package descriptions and
docs must continue to say development preview and must not claim version 1.0.

## Scope boundaries

This program adds no external dependency, network service, telemetry, runtime C
ABI, IL opcode, grammar, verifier rule, or cross-layer dependency. Procedural
art/audio remain deterministic fallbacks. Runtime or workflow changes outside
the game still require the repository's normal ADR and platform-policy process.
