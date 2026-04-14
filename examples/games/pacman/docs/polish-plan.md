# Crackman Demo Polish Plan

## Goal

Take `examples/games/pacman` from a functional but visibly prototype-grade demo to a polished, arcade-premium Crackman game that still fits Viper's native 2D canvas model.

The target is not "more color." The target is:

- a clear arcade visual identity
- strong typography and logo treatment
- aligned, intentional menus and HUD
- a maze that feels authored instead of debug-drawn
- readable, expressive character art and animation
- overlays and transitions with real production value
- a renderer structure that can support future polish without keeping all UI logic trapped in `game.zia`

## Current State Audit

The gameplay foundation is better than the presentation suggests. The demo already has the important systems in place:

- full game-state flow through `StateMachine`
- menu selection through `ButtonGroup`
- action-mapped input through `Viper.Input.Action`
- tweening, timers, smooth score display, particles, and screen FX
- distinct entity modules for maze, player, ghosts, fruit, and particles

That means the demo is not "empty." It is mostly suffering from a prototype presentation layer.

### What is working

- The maze, ghost logic, frightened mode, fruit spawn, death flow, score progression, and level loop already exist.
- `particles.zia` and the screen-fx path already provide a real base for juice.
- The code is split enough that entity art upgrades can happen without rewriting movement or AI.
- The runtime already exposes the tools needed for a strong polish pass:
  - gradients and alpha boxes/discs
  - clipping
  - bitmap fonts
  - pixel buffers and sprite-style blits
  - timers and tweens
  - particle emitters

### What is currently weak

1. The menu and high-score screens are plain text on black with hardcoded coordinates.
2. The game viewport has no scene composition beyond `clear -> maze -> entities -> HUD text`.
3. The HUD is only three text strings at the top edge.
4. The maze is rendered as flat blue tile boxes, so it reads like a tile debug pass rather than a finished arcade board.
5. Crackman, ghosts, and fruit are readable, but all of them still look like placeholder vector primitives.
6. There is almost no menu animation or attract-mode behavior.
7. Overlay states like `READY`, `PAUSED`, `GAME OVER`, and `LEVEL COMPLETE` are plain centered text.
8. The visual constants live in `config.zia` as raw colors instead of a real theme system.
9. Presentation and input hit regions are centralized in `game.zia`, so layout and behavior are tightly coupled.
10. Boot flow still prints terminal text in `main.zia`, which breaks the illusion of a finished game.

## Concrete Problems Found In Code

- `renderMenu()` in [game.zia](/Users/stephen/git/viper/examples/games/pacman/game.zia#L284) is a flat stack of `canvas.Text()` calls plus decorative discs. There is no card layout, no marquee, no hover/selection shell, and no animated attract layer.
- `renderHighScores()` in [game.zia](/Users/stephen/git/viper/examples/games/pacman/game.zia#L330) is also plain text on black with fixed offsets and no framing.
- The main in-game render path in [game.zia](/Users/stephen/git/viper/examples/games/pacman/game.zia#L651) clears black, draws the maze and entities, then drops text overlays directly into the center of the screen. It has no background atmosphere, no board frame, no dedicated HUD region, and no modal shell.
- `drawUI()` in [game.zia](/Users/stephen/git/viper/examples/games/pacman/game.zia#L704) is only score, level, and lives as raw text. That is too bare for a polished arcade game.
- `Maze.draw()` in [maze.zia](/Users/stephen/git/viper/examples/games/pacman/maze.zia#L164) renders walls as full blue boxes, dots as tiny discs, pellets as blinking discs, and the gate as a pink box. There is no wall shaping, line treatment, glow, or framing.
- `PacMan.draw()` in [player.zia](/Users/stephen/git/viper/examples/games/pacman/player.zia#L165) is a yellow disc with a triangular mouth cutout. Functional, but still unmistakably prototype art.
- `Ghost.draw()` in [ghost.zia](/Users/stephen/git/viper/examples/games/pacman/ghost.zia#L348) uses a disc plus box body with simple eyes. The logic is fine; the art direction is not finished.
- `Fruit.draw()` in [fruit.zia](/Users/stephen/git/viper/examples/games/pacman/fruit.zia#L111) is a colored disc with a stem. That is enough for mechanics, not enough for presentation.
- `main.zia` in [main.zia](/Users/stephen/git/viper/examples/games/pacman/main.zia#L31) still prints terminal banners and "Loading..." messages before opening the game.

## Recommended Visual Direction

Do not aim for "clean modern flat UI." Pac-Man wants arcade energy.

Recommended direction:

- deep midnight backdrop instead of empty black
- neon maze treatment with layered blue/cyan glow, not flat wall boxes
- saturated arcade marquee colors: electric blue, hot pink, danger red, citrus yellow
- bold bitmap typography with a real Crackman-style logo treatment
- animated menu scene that feels like attract mode, not a setup screen
- expressive sprite-like characters with shadows, eye direction, and motion accents
- scoreboard and overlays framed like cabinet UI, not debug text

This should feel like a commercial retro arcade remake, not a runtime feature demo.

## Strategic Decisions

### 1. Keep this on `Viper.Graphics`, not `Viper.GUI`

The game is already custom-rendered. The board, characters, pellets, and overlays all belong in the same drawing model. Bringing `Viper.GUI` into this would split the visual system and make it harder to achieve a cohesive arcade look.

The right move is to build a compact internal canvas UI layer for menu, HUD, and overlays.

### 2. Use bundled bitmap fonts and logo art

The default built-in text is the single biggest reason the menu and HUD look cheap.

Add bundled font assets under:

- `examples/games/pacman/assets/fonts/`

Use:

- a bold display bitmap font for logo, headings, state cards
- a readable arcade/pixel sans for HUD, score table, and small controls

The Pac-Man logo itself should be treated like a real title graphic, not plain text. That can be:

- a pre-rendered pixel-art logo asset, or
- a custom assembled vector/logo renderer if asset-free is required

### 3. Prefer sprite-backed character art for the final pass

For this game, the polish ceiling is much higher if Pac-Man, ghosts, fruit, and menu ornaments use curated sprite sheets or pre-rendered pixel buffers rather than only immediate-mode circles and boxes.

Recommended asset split:

- character sprites
- fruit sprites
- logo and menu ornaments
- optional maze corner/edge glyph masks if the wall system stays tile driven

Procedural rendering can remain as a fallback or debug path, but it should not be the primary finished look.

### 4. Preserve gameplay readability over visual effects

Pac-Man is fast and grid-driven. The polish pass should add life, not visual confusion.

That means:

- no excessive camera shake
- no low-contrast pellets
- no noisy background behind the maze
- no overdraw that obscures ghost state or turn decisions

## Architecture Changes Needed Before The Big Visual Pass

### A. Split presentation out of `game.zia`

Add a real UI/render surface under:

- `ui/theme.zia`
- `ui/layout.zia`
- `ui/widgets.zia`
- `ui/menu_scene.zia`
- `ui/hud.zia`
- `ui/renderer.zia`
- `ui/sprites.zia`

Responsibilities:

- `theme.zia`
  - palette
  - font handles
  - spacing scale
  - glow/shadow colors
  - timing constants for UI animation
- `layout.zia`
  - marquee/header rects
  - maze frame rect
  - HUD card rects
  - footer/help rail
  - modal and score table regions
- `widgets.zia`
  - panel shells
  - buttons
  - segmented selection rows
  - score rows
  - badges and pills
  - modal shells
- `menu_scene.zia`
  - attract animation
  - title layout
  - menu button rendering
  - high-score screen rendering
- `hud.zia`
  - score card
  - lives display
  - level badge
  - fruit history / upcoming fruit display
- `renderer.zia`
  - scene composition and top-level render order
- `sprites.zia`
  - load/caching of sprite assets or pre-rendered pixel buffers

This is mandatory. Without it, any "polish" pass will just make `game.zia` larger and harder to maintain.

### B. Separate named layout rects from input logic

The demo currently relies on simple state/button selection logic rather than a real visual-layout model. Menu hit regions, overlay buttons, and future mouse support should come from named rect helpers in `layout.zia`, not duplicated numeric literals.

That gives:

- alignment that survives iteration
- hover/press states
- safe UI rewrites
- future controller and mouse parity

### C. Add a small presentation-state model

The gameplay already has a state machine. The renderer still needs a UI presentation layer for:

- menu logo pulse
- menu button hover/active state
- high-score reveal
- overlay fade in/out
- fruit badge pop
- ready/go banner motion
- win/game-over card transitions

This can stay lightweight, but it should exist as real state instead of ad hoc timers embedded in every draw call.

## Massive Polish Scope

### Phase 1: Foundation, Theme, and Layout Rewrite

#### Objectives

- replace raw color constants with a coherent visual theme
- establish named layout zones
- stop treating menu and HUD as loose text strings

#### Concrete work

- Introduce a real `ui/` layer and keep game rules/gameplay in the current modules.
- Expand the presentation framing around the existing 28x31 grid instead of drawing directly against screen edges.
- Keep the playable maze size intact, but add:
  - a marquee/top banner
  - a dedicated HUD band
  - a footer/help strip
  - board framing and side breathing room
- Move raw presentation colors out of `config.zia` into a theme module.
- Load bundled bitmap fonts on startup.
- Stop printing terminal status banners from `main.zia`; the game should boot straight into the visual shell.

#### Acceptance criteria

- no screen is just text on a black background
- all visible UI aligns to named regions
- fonts, spacing, colors, and button shells are centralized

### Phase 2: Menu And Attract-Mode Overhaul

#### Objectives

- make the menu look like a finished arcade title screen
- add motion immediately, before the player even starts a game

#### Concrete work

- Replace the current menu with:
  - animated logo/marquee
  - attract lane showing Pac-Man chased by ghosts
  - large primary CTA
  - framed secondary actions
  - live high-score spotlight card
  - subtle animated pellets / scanline / glow treatment
- Replace the high-score screen with:
  - framed leaderboard card
  - rank styling
  - champion emphasis
  - clearer return prompt
- Add menu motion:
  - logo bob or pulse
  - button selection sweep
  - ghost eye tracking / chase loop
  - subtle background twinkle

#### Acceptance criteria

- the menu is visually recognizable in a screenshot as a real game title screen
- the selected option is obvious without relying only on yellow text
- the menu has at least two layers of motion, not just static text

### Phase 3: Maze Rendering Redesign

#### Objectives

- turn the maze from tile debug geometry into authored arcade scenery

#### Concrete work

- Replace flat wall boxes with a wall-shaping renderer:
  - detect wall neighbors
  - draw corner, straight, T-junction, and endcap shapes
  - use rounded neon linework or beveled wall fills
- Add layered maze treatment:
  - deep background field
  - outer frame
  - inner glow
  - subtle tunnel emphasis
  - cleaner ghost-house gate art
- Improve dot and pellet rendering:
  - softer pellet blink
  - slight halo on power pellets
  - cleaner dot sizing against the upgraded maze
- Add per-level palette variation later if desired, but only after the base theme is stable.

#### Acceptance criteria

- walls no longer read as full-tile blue blocks
- pellets are readable without overpowering the board
- the maze looks good both in motion and in still captures

### Phase 4: Character And Item Art Upgrade

#### Objectives

- give Pac-Man, ghosts, and fruit a finished visual identity

#### Concrete work

- Upgrade Pac-Man:
  - directional eye or highlight
  - better mouth timing
  - subtle squash/stretch on movement
  - stronger death presentation
- Upgrade ghosts:
  - proper ghost silhouette
  - more expressive eye direction
  - frightened-face upgrade
  - animated foot or bob cycle
  - better flash/fear visuals
- Upgrade fruit:
  - real sprite-style fruit art per type
  - better collected-points display treatment
- Add per-entity shadow/halo rules where helpful, but keep the board readable.

#### Acceptance criteria

- the characters no longer look like generic discs and boxes
- frightened, eaten, and normal ghost states are visually distinct at a glance
- fruit looks collectible and specific, not like a placeholder icon

### Phase 5: HUD, Overlays, And Score Presentation

#### Objectives

- replace the debug-HUD look with cabinet-style presentation

#### Concrete work

- Replace `drawUI()` with real HUD cards or rails:
  - score
  - high score
  - level
  - lives with mini Pac-Man icons
  - fruit progress history
- Redesign overlays:
  - `READY`
  - `PAUSED`
  - `GAME OVER`
  - `LEVEL COMPLETE`
  - `OUCH`
- Use modal cards, banners, or framed strips instead of raw centered text.
- Add proper text hierarchy and animation:
  - fade and slide
  - bounce only where it fits the arcade feel
  - short settle timing, not slow RPG menus

#### Acceptance criteria

- HUD looks intentional and balanced against the maze
- overlays feel like part of the game shell, not debug annotations

### Phase 6: Juice And Motion Pass

#### Objectives

- make the game feel alive without hurting clarity

#### Concrete work

- Expand existing particle use:
  - richer pellet burst
  - better ghost-eat burst
  - fruit sparkle upgrade
  - level-complete celebration
- Add presentational motion:
  - menu attract loop
  - subtle ghost bob
  - Pac-Man movement accent
  - overlay enter/exit motion
  - score-count pulse
- Add environmental polish:
  - background shimmer behind marquee
  - maze glow pulse on level start
  - tunnel shimmer or soft vignette

#### Acceptance criteria

- menu and gameplay both have visible motion layers
- effects support gameplay feedback instead of obscuring it

### Phase 7: Boot Flow, Framing, And Final Finish

#### Objectives

- remove remaining demo scaffolding and make the package feel complete

#### Concrete work

- Remove terminal `Say()` boot chatter from `main.zia`.
- Replace it with immediate visual startup.
- Clean up help text and controls presentation so they live inside the game shell.
- Make sure pause, game over, and return-to-menu flow all share the same presentation language.
- Rebuild the native demo binary after the visual pass and verify it alongside the other demos.

#### Acceptance criteria

- no part of the game feels like a tech demo banner
- menus, game, overlays, and exit flow read as one product

## Asset Plan

Recommended additions:

- `assets/fonts/`
  - display font
  - body/HUD font
- `assets/ui/`
  - logo
  - decorative marquee accents
  - optional background masks/noise overlays
- `assets/sprites/`
  - Pac-Man frames
  - ghost frames and frightened variants
  - fruit set
  - mini HUD icons

If external art is not acceptable, use pre-rendered `Pixels` caches generated at startup. That is still better than redrawing every character from the current primitive shapes in their present form.

## Implementation Order

The pragmatic order is:

1. theme, fonts, layout, widget primitives
2. menu and high-score rewrite
3. renderer split and HUD rewrite
4. maze renderer upgrade
5. character/fruit sprite upgrade
6. overlay and transition pass
7. final motion and boot-flow cleanup

Do not start with character art first. If the menu, HUD, and maze frame stay prototype-grade, better sprites will still look dropped into an unfinished shell.

## Verification Plan

The polish pass needs more than "it compiles."

Verify:

- menu readability at a glance
- gameplay readability during frightened mode and heavy motion
- HUD clarity while moving at full speed
- overlay clarity on pause/game-over/win states
- no visual hit region drift after layout changes
- native demo build still succeeds through the demo scripts

Suggested checkpoints:

- save menu screenshots before and after
- save gameplay screenshots before and after
- smoke test the playable loop after each large renderer split
- rebuild the native demo binary when the pass is complete

## Finish Criteria

This should be considered finished only when:

- the menu looks like a real title screen
- the maze no longer looks flat and generic
- the HUD and overlays look authored and aligned
- Pac-Man, ghosts, and fruit feel visually deliberate
- the game can be shown in screenshots without apologizing for the presentation
