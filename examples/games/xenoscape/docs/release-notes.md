# XENOSCAPE 1.0 release notes

Version 1.0 turns the original systems demo into a cohesive ten-region action
Metroidvania while retaining its zero-dependency procedural foundations. The
commercial-quality upgrade is organized around 28 shipped improvements:

1. A cinematic authored title treatment with a deterministic parallax fallback.
2. A complete title, pause, options, extras, credits, and confirmation shell.
3. Three explicit expedition profiles with New, Load, Continue, Delete, and
   overwrite workflows.
4. Atomic schema-v2 saves, migration defaults, timestamps, corruption backup,
   autosave points, and truthful slot summaries.
5. A twelve-route nonlinear world graph joining all ten campaign regions.
6. Eight stable named rooms per region, persistent discovery, return routes,
   shortcuts, and fast travel.
7. An evolving shipwreck hub with Map, Workbench, Archive, Simulator, and
   Launch stations.
8. Banked salvage and four permanent three-tier upgrade branches with exact
   costs and bounded effects.
9. Explorer, Standard, and Veteran profiles with live lowering and queued
   difficulty increases.
10. Checkpoint respawn, permanent-progress retention, and recoverable salvage in
    place of finite lives.
11. Fully gated Wall Jump, Double Jump, Dash, Charge Shot, Ground Pound, and
    Grapple traversal.
12. Functional ice, quicksand, crumble, conveyors, steam, destructibles,
    one-way platforms, switches, keyed doors, shrines, teleporters, and lore.
13. Enemy-specific damage, immunity, status, scoring, death effects, tells, and
    readable projectile behavior.
14. Four major multi-phase bosses plus a regional combat or narrative climax in
    every area.
15. Velocity look-ahead, delayed vertical intent, corner correction, wall-stick
    grace, and Reduced Motion camera behavior.
16. Hit stop, screen shake, controller rumble, particles, room titles, floating
    score feedback, and distinct interaction toasts.
17. A responsive HUD with room identity, regional objectives, discoveries,
    boss cues, minimap, and high-contrast presentation.
18. Persistent contextual tutorials driven by demonstrated semantic actions.
19. A playable skippable opening, stable lore/objective ids, evolving Archive,
    and nonblocking subtitle-backed ARIA radio.
20. First-party key art, ARIA portrait, player/tiles/icon atlases, UI texture,
    provenance manifest, and tested procedural fallbacks.
21. A stable 41-frame player layout covering six-frame run, turn, land, shoot,
    charge, dash, grapple, pound, hurt, death, and acquisition poses.
22. Ten palette-graded biome presentations with layered backgrounds, ambient
    motion, foreground dressing, localized lighting, and landmarks.
23. Independent music, SFX, ambience, and voice intent; ten regional identities;
    hub/boss/victory/game-over cues; adaptive combat intensity; and material
    footsteps.
24. Per-region D-through-S ranks based on time, damage, discoveries, and optional
    completion instead of cumulative score alone.
25. Read-only regional Time Trials that commit only improved best results.
26. A four-encounter Boss Rush with continuous time/damage and its own ranked
    debrief.
27. Double-confirmed New Game+, denser/stronger encounters, normal and true
    endings, and reachable event hooks for all 32 achievements.
28. Release metadata, standalone packaging, runtime toggles, player/developer
    documentation, asset generation, screenshot/package/performance gates, and
    VM/native CTest coverage.

## Compatibility

The game remains self-contained and uses only Viper runtime facilities. All
production paths and fallbacks are designed for macOS, Windows, and Linux; no
network access or package download is required at build or runtime.

## Save compatibility

Older additive saves migrate to schema 2 defaults on load. Existing core keys
remain stable. A migrated profile receives its last-write timestamp on the next
successful save, and unreadable payloads are preserved beside the live file with
a `.corrupt.bak` suffix before safe defaults are restored.
