# Chess Demo Polish Plan

## Goal

Take `examples/games/chess` from a functional but visibly prototype-grade demo to a polished, premium-looking chess game that still fits Viper’s native 2D canvas model.

The target is not “more decoration.” The target is:

- a clear visual identity
- strong typography
- aligned layout and spacing
- readable, attractive pieces and board
- menus and overlays that feel intentional
- no fake or misleading controls
- a renderer structure that can support future polish without turning into coordinate soup

## Current State Audit

The current demo is structurally sound as a chess game, but the presentation layer is still prototype UI.

### What is working

- The game loop, board logic, move generation, AI, drag-and-drop, promotion, and overlays are already in place.
- The renderer is isolated in `ui/renderer.zia`, which gives us a workable seam for a visual overhaul.
- The runtime already exposes the tools we need:
  - `Canvas.GradientH` / `Canvas.GradientV`
  - alpha fills like `Canvas.BoxAlpha`, `Canvas.DiscAlpha`, `Canvas.EllipseAlpha`
  - rounded frames and thick lines
  - `Canvas.SetClipRect`
  - `BitmapFont.LoadBDF` / `LoadPSF`
  - `Canvas.TextFont*`
  - `Pixels.*` operations including `Tint`, `Scale`, `Blur`, `Resize`, `BlitAlpha`

### What is currently weak

1. The visual system is almost entirely raw constants in `core/config.zia`.
2. The UI uses the default built-in text everywhere, which makes the game look like a debug tool instead of a finished title.
3. The board is flush-left with almost no framing or atmosphere.
4. The side panel is cramped and visually flat.
5. Captures are represented as generic dots instead of actual piece icons.
6. The menu is a stack of hardcoded buttons with weak alignment and no hierarchy.
7. Overlays and dialogs are functional but visually blunt.
8. Piece sprites are readable, but they are too flat and generic to carry the whole experience.
9. Many hit targets are hardcoded in `ui/game.zia`, so layout and input are tightly coupled.
10. Some UI elements are decorative only and therefore misleading.

### Concrete problems found in code

- `drawTopBar()` and `drawBottomBar()` render buttons like `New`, `Menu`, `Draw`, and `Resign`, but game input does not route clicks to them. They currently look interactive without actually being interactive.
- `drawPlayerSection()` positions the clock with `pw - 60` instead of `px + pw - ...`, so panel alignment is structurally wrong.
- `drawMainMenu()` is one large hardcoded composition of `RoundBox` and `Text` calls with no reusable button/card primitives.
- `drawMoveHistory()` is not clipped or framed as a real scrollable/log panel.
- The current renderer has no concept of hover, press, focus, or active visual state for UI controls.
- The piece art path in `ui/pieces.zia` is still “clean prototype iconography,” not finished art direction.

## Recommended Visual Direction

Use a warm, premium tournament-broadcast look instead of generic flat blue panels.

Recommended direction:

- Board: walnut / maple palette, softer highlights, better edge framing
- Chrome: dark graphite and brass, not plain navy rectangles
- Typography: bitmap serif or elegant square-grotesk pairing, not the built-in 8x8 font
- Pieces: premium Staunton-inspired silhouettes with stronger contrast, shadow, and material feel
- Panels: card-based layout with subtle depth, separators, and hierarchy
- Overlays: centered modal cards with dimmed scrim, not plain dark boxes

This should feel like a playable chess product, not a test harness.

## Strategic Decisions

### 1. Keep the game on `Viper.Graphics`, not `Viper.GUI`

The board, overlays, move hints, and drag interactions are already custom canvas work. Mixing them with GUI widgets would create two visual systems and make the result harder to unify. The right move is to build a small internal UI toolkit on top of `Canvas`, not to switch the demo over to `Viper.GUI`.

### 2. Use bitmap fonts

The current built-in text is the single biggest reason the menus and panels look cheap. We should add bundled bitmap font assets under an `assets/fonts` directory and render all major UI through `Canvas.TextFont*`.

Recommended font split:

- title / large labels: bold serif or sharp display bitmap font
- body / move list / controls: clean bitmap sans or terminal-style font with better spacing than the default

### 3. Prefer real art assets for the final piece theme

Viper can load PNGs and alpha-blit them cleanly. If the goal is a visibly polished chess game, the best result is:

- ship a curated piece set as PNG assets
- optionally keep the procedural piece generator as a fallback or debug theme

If “no external art assets” is a hard requirement, we can still improve `ui/pieces.zia`, but the polish ceiling is lower.

## Architecture Changes Needed Before the Big Visual Pass

### A. Split theme, layout, and widgets out of `renderer.zia`

Add:

- `ui/theme.zia`
- `ui/layout.zia`
- `ui/widgets.zia`

Responsibilities:

- `theme.zia`
  - color palette
  - spacing scale
  - radii
  - shadows
  - font handles
  - piece-theme selection
- `layout.zia`
  - derive board, panel, rail, card, and modal rects from window size
  - centralize hitboxes instead of scattering pixel literals
- `widgets.zia`
  - reusable button drawing
  - segmented control drawing
  - panel/card drawing
  - label/value rows
  - modal shell
  - badge/pill components

This is mandatory. Without it, any polish pass will just create a bigger hardcoded renderer.

### B. Separate UI hit-testing from raw pixel literals

Menu clicks, HUD clicks, promotion choice clicks, and game-over actions should use named rect helpers from `layout.zia`, not repeated numeric ranges in `game.zia`.

That gives us:

- aligned visuals and hitboxes
- safe iteration on layout
- hover states
- future keyboard/controller navigation

### C. Introduce a small UI state model

Add hover/press tracking for:

- main menu mode buttons
- difficulty buttons
- start button
- top rail buttons
- bottom action buttons
- promotion choices
- game-over buttons

The current renderer only knows static active state. It needs real interaction state if it is going to look polished.

## Massive Polish Scope

### Phase 1: Foundation and Layout Rewrite

#### Objectives

- Replace scattered layout constants with derived named regions
- Fix non-functional and misaligned controls
- Introduce fonts, theme, cards, and button primitives

#### Concrete work

- Expand the window from `960x700` to a roomier presentation size, likely `1180x760` or `1280x800`
- Re-center the board with real margins instead of pinning it to the left edge
- Build a consistent frame:
  - top rail
  - board zone
  - side analysis panel
  - bottom status strip or integrated footer
- Remove dead controls or wire them for real:
  - `New`
  - `Menu`
  - `Draw`
  - `Resign`
- Replace the current `drawMainMenu()` with:
  - hero title
  - centered configuration card
  - aligned segmented controls
  - strong start CTA
- Add font loading in startup and pass fonts through the theme

#### Acceptance criteria

- no visible UI element looks clickable unless it is clickable
- all menu elements align on a small set of shared columns
- all panel text uses custom fonts
- no hitbox in `game.zia` depends on duplicated magic numbers

### Phase 2: Board and Piece Art Upgrade

#### Objectives

- Make the board feel premium and readable
- Make the piece set feel intentional instead of generic

#### Concrete work

- Add a richer board treatment:
  - outer frame
  - inset playing surface
  - subtle square bevel or tonal variation
  - softer coordinate labels
  - better move highlight colors
- Replace harsh prototype highlight colors with a cohesive palette:
  - selected square
  - legal move dot
  - capture ring
  - last move
  - check danger
- Rework piece rendering via one of two paths:

Path A, recommended:
- add themed PNG piece assets
- pre-load once
- optionally add a soft drop shadow buffer per piece

Path B, fallback:
- overhaul `ui/pieces.zia` with layered silhouettes, shadow passes, highlight passes, and better internal detail
- use `Pixels.Blur`, `Pixels.Tint`, and alpha blits to fake material depth

#### Acceptance criteria

- the board still reads cleanly at a glance
- piece contrast is strong on both light and dark squares
- the game looks materially better in screenshots, not just in motion

### Phase 3: Side Panel Redesign

#### Objectives

- Turn the side panel into a real game companion panel, not a list of text blocks

#### Concrete work

- Replace the current flat panel with stacked cards:
  - black player card
  - move list card
  - white player card
  - game settings/status card
- Fix the clock alignment bug and make clocks a real visual element
- Replace captured-piece dots with mini piece icons
- Improve move history with:
  - a framed surface
  - aligned columns
  - current move emphasis
  - clipping and optional scroll behavior
- Add useful metadata:
  - side to move badge
  - mode
  - difficulty
  - optional opening label later

#### Acceptance criteria

- clocks, names, captures, and metadata align cleanly
- captures communicate actual material, not just count
- move list feels like a designed component rather than debug text

### Phase 4: Menus and Modal Overlays

#### Objectives

- Make the menu, promotion chooser, game-over screen, and AI-thinking state look production-ready

#### Concrete work

- Main menu:
  - background gradient or board vignette
  - centered configuration card
  - segmented options with hover/active states
  - stronger visual hierarchy
- Promotion dialog:
  - centered modal card with scrim
  - labeled piece choices
  - hover highlight and clearer selection affordance
- Game-over overlay:
  - winner headline
  - reason subtext
  - better CTA buttons
  - avoid plain board-obscuring rectangle
- Thinking state:
  - unobtrusive status pill or side-panel state instead of a floating debug box

#### Acceptance criteria

- overlays feel like part of the same visual system as the board
- no modal feels like a temporary programmer box

### Phase 5: Interaction Polish and Motion

#### Objectives

- Make the game feel alive without turning it into a complicated animation project

#### Concrete work

- Add hover states for all clickable UI
- Add press states for buttons
- Add drag lift effect for picked-up pieces:
  - soft shadow
  - slight scale-up or offset illusion
- Add subtle move animation:
  - short slide for moved piece
  - optional fade for captures
- Add subtle check pulse or warning frame on checked king
- Add turn transition polish:
  - active player card glow
  - AI thinking state integrated into opponent card

#### Acceptance criteria

- the game feels responsive before any sound is added
- animations are short and supportive, not flashy

### Phase 6: Finish Pass

#### Objectives

- Eliminate leftover prototype seams

#### Concrete work

- unify spacing and radii across all screens
- audit every text baseline and edge alignment
- review contrast for all text and indicators
- remove leftover raw colors from gameplay code
- ensure screenshots look good at default window size

## Detailed Component Plan

### Board Surface

Replace the current `drawBoard()` approach with a layered composition:

1. window background gradient
2. board frame / shelf
3. board surface
4. square pattern
5. coordinate labels
6. highlights
7. pieces
8. drag piece
9. board overlay states

The board should feel like an object sitting in a designed scene, not a grid dropped on a solid background.

### Buttons

All buttons should share a single draw path with:

- idle
- hover
- pressed
- active/selected
- disabled

The current demo draws each button separately with hand-tuned text positions. That should be removed.

### Player Cards

Each player card should have:

- player name
- side color icon
- active-turn indicator
- clock
- captured material row
- optional “thinking” / “check” / “winner” status badge

### Move List

The move list should become a real component:

- clipped region
- aligned move number / white / black columns
- current move emphasis
- enough spacing to breathe

### Promotion UI

Promotion choices should use the same piece art system as the board and feel like deliberate selection tiles, not generic boxes.

## Asset Plan

Add:

- `examples/games/chess/assets/fonts/`
- `examples/games/chess/assets/pieces/` if using PNG piece art
- optionally `examples/games/chess/assets/ui/` for logo/title treatment

Recommended rule:

- keep assets minimal and high leverage
- fonts and piece art are the two asset categories with the highest payoff

## Testing and Verification Plan

Visual polish still needs engineering checks.

### Functional checks

- all visible buttons perform an action
- no menu hitbox drifts after layout refactor
- promotion and game-over CTAs still work
- move drag/drop still behaves correctly

### Build/runtime checks

- `viper run examples/games/chess`
- smoke probe still passes
- native build path still runs if the demo is part of demo builds

### Review checklist

- title screen screenshot quality
- midgame screenshot quality
- promotion dialog screenshot quality
- checkmate overlay screenshot quality
- readability at default size

## Recommended Implementation Order

1. Add theme, layout, and widget primitives
2. Add bitmap font assets and replace default text
3. Rebuild menu and HUD with real interaction states
4. Redesign side panel and move list
5. Upgrade board surface
6. Upgrade piece art
7. Rework overlays
8. Add interaction polish and light animation
9. Final alignment and screenshot pass

## Stretch Goals After the Main Polish

- board orientation flip for playing black
- move-list scrolling
- coordinate fade or minimalist mode
- opening name display
- evaluation bar
- subtle sound design
- piece theme switching
- board theme switching

## Bottom Line

The right way to polish this demo is not to tweak colors inside the current renderer. The right way is:

- build a real visual system
- fix fake/nonfunctional controls
- introduce proper typography
- redesign the layout around reusable components
- upgrade board and piece art with either strong procedural rendering or real assets

That is what will move this from “ugly demo” to “polished chess game.”
