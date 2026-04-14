# Viper Crackman

A maze-chase arcade demo written in Zia, using Viper's game and graphics runtime.

## Current Direction

The demo now has a stronger presentation foundation on top of the original gameplay base:

- state-machine-driven menu and gameplay flow
- action-mapped input
- tile maze logic
- ghosts, frightened mode, fruit, score progression, and particles
- procedural audio via `Viper.Sound`
- custom canvas UI/theme/layout modules for the ongoing visual overhaul

The next major work should continue the polish pass rather than expand gameplay scope.

## Design And Polish Planning

- [Polish Plan](polish-plan.md) — large-scale visual, UI, animation, and renderer-architecture overhaul plan for the demo

## Core Source Layout

```
crackman/
    main.zia            Entry point
    game.zia            State flow, input, update loop, top-level render dispatch
    config.zia          Constants and gameplay configuration
    sound.zia           Procedural SFX/music manager
    maze.zia            Maze data and current tile-based maze rendering
    player.zia          Crackman movement and drawing
    ghost.zia           Ghost logic and drawing
    fruit.zia           Fruit spawning and drawing
    particles.zia       Particle effects

    ui/
        theme.zia       Palette, bitmap fonts, text helpers
        layout.zia      Named screen and panel regions
        widgets.zia     Reusable canvas UI primitives
        sprites.zia     Cached Pixels art for walls, characters, items, and menu icons
        renderer.zia    Menu, HUD, backdrop, and overlay rendering
```

## Running

```bash
cd examples/games/pacman
viper run .
```
