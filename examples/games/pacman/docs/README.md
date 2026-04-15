# Viper Crackman

A maze-chase arcade demo written in Zia, using Viper's game and graphics runtime.

## Current Direction

The demo now has a stronger presentation foundation on top of the original gameplay base:

- state-machine-driven app flow split across gameplay and frontend controllers
- action-mapped input
- tile maze logic with runtime pathfinding and batched maze rendering
- ghosts, frightened mode, fruit, score progression, and particles
- combo/reward feedback and stronger HUD progression telemetry
- persistent profile data with saved leaderboard, run summaries, achievement unlocks, rank, XP, and rotating contracts
- procedural audio via `Viper.Sound`, split into music-track and SFX builders
- saved options for fullscreen, master volume, music volume, SFX volume, and mute
- custom canvas UI/theme/layout modules for the ongoing visual overhaul

The next major work should continue the polish pass rather than expand gameplay scope.

## Design And Polish Planning

- [Polish Plan](polish-plan.md) — large-scale visual, UI, animation, and renderer-architecture overhaul plan for the demo

## Core Source Layout

```
crackman/
    main.zia            Entry point
    game.zia            Top-level app orchestration and state handoff
    session.zia         Gameplay session state, update logic, and in-game rendering
    frontend.zia        Menu/profile/options/summary controller and UI flow
    progression.zia     Persistent profile, leaderboard, achievements, rank, XP, and contracts
    settings.zia        Saved display/audio settings persistence
    config.zia          Constants and gameplay configuration
    sound.zia           Audio orchestration and mix control
    maze.zia            Maze data, collision rules, batched rendering, and ghost pathfinding
    player.zia          Crackman movement and drawing
    ghost.zia           Ghost AI, house logic, and drawing
    fruit.zia           Fruit spawning and drawing
    particles.zia       Particle effects
    utils.zia           Shared grid movement and coordinate helpers

    audio/
        sfx_bank.zia    Procedural SFX registration
        music_tracks.zia MusicGen track builders

    ui/
        theme.zia       Palette, bitmap fonts, text helpers
        layout.zia      Named screen and panel regions
        widgets.zia     Reusable canvas UI primitives
        sprites.zia     Cached sprite facade for the rest of the game
        sprite_support.zia Shared sprite helper functions
        sprite_entities.zia Crackman and ghost sprite builders
        sprite_items.zia Dot, pellet, gate, and fruit sprite builders
        sprite_maze.zia Wall tile sprite builders
        renderer.zia    Menu, HUD, backdrop, and overlay rendering
```

## Running

```bash
cd examples/games/pacman
viper run .
```
