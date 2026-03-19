# YouTube Video Plan: Introducing Viper to the Public

## Context

Viper is a from-scratch compiler toolchain — two language frontends (Zia and BASIC), a typed IL, VM, native codegen for x86_64 and ARM64, and a 226-class runtime — all with zero external dependencies. This video introduces it to a broad developer audience with a demos-first approach.

---

## Video Structure

**Target length:** 8–10 minutes
**Tone:** "Look what I built" — enthusiastic, grounded, not lecture-y.
**Audience:** Developers, CS students, programming language enthusiasts.
**Core principle:** Let the demos sell it. Technical details are seasoning, not the main course.

---

### ACT 1: COLD OPEN (0:00 – 1:00)

**Goal:** Grab attention with visuals before any explanation.

- **0:00 – 0:30**: Open directly on the **graphics-show** demo. No talking for the first 5 seconds — just fireworks, matrix rain, or starfield running full screen. Then start narrating over it:
  *"This is running on a compiler I built from scratch. No LLVM. No SDL. No OpenGL. Zero dependencies."*

- **0:30 – 1:00**: Quick elevator pitch + architecture diagram on screen:
  *"Viper is a complete compiler toolchain — two programming languages, an optimizer, native code generation for x86 and ARM, and a runtime library with over 200 classes. Everything — the graphics, the networking, the audio — all written from scratch."*
  ```
  Zia ──┐              ┌── VM (interpret)
        ├── Viper IL ──┼── x86_64 (native)
  BASIC ┘              └── ARM64  (native)
  ```

---

### ACT 2: THE LANGUAGE — ZIA IN ACTION (1:00 – 2:30)

**Goal:** Show Zia is a real language through a rapid code tour, then jump into a demo.

- **1:00 – 1:45**: Quick code tour — show 3-4 snippets on screen (no live typing needed, just pre-prepared code with narration):
  1. Hello world (simple)
  2. An entity with methods (OOP)
  3. Pattern matching with enums
  4. A Canvas graphics call
  - Narrate the highlights: *"Zia has entities, generics, pattern matching, enums — it's a modern, statically typed language."*
  - Keep it fast. 10-15 seconds per snippet. Don't explain syntax — show breadth.

- **1:45 – 2:30**: **Live demo: Chess**
  - Launch the graphical chess game. Play 2-3 moves against the AI.
  - While playing: *"This is a full chess engine with alpha-beta pruning and transposition tables. The GUI, the AI, the rendering — all Zia, all running on the Viper runtime."*

---

### ACT 3: DEMO MONTAGE (2:30 – 4:30)

**Goal:** Show range and polish through a quick-hit tour of demos.

Show 3-4 demos, ~30-45 seconds each. For each one, launch it, show it running, and give a one-liner about what makes it interesting:

- **Pac-Man** (~30s): *"Ghost AI with scatter, chase, and frightened states. BFS pathfinding. Input debouncing."*
- **Sidescroller/Platformer** (~30s): *"Physics, parallax scrolling, particle effects, boss fights, a full level progression system."*
- **Paint app** (~30s): *"Eight drawing tools, color picker, brush size, undo stack — a real desktop application."*
- **Centipede** (~30s): *"Arcade-style action with sprite rendering and particle effects."*

End the montage with a pause: *"All of these are written in Zia. But Viper isn't just one language."*

---

### ACT 4: THE TWIST — BASIC (4:30 – 6:00)

**Goal:** Reveal the multi-language story. This is the "aha" moment of the video.

- **4:30 – 5:00**: *"Viper has a second frontend: BASIC."*
  Show a side-by-side: a simple program written in both Zia and BASIC. Highlight that they compile to the same IL.
  *"Both languages compile to the exact same intermediate language. Same optimizer. Same runtime. Same native backends."*

- **5:00 – 5:30**: **vTRIS demo** — Run the Tetris clone. Show the polished menu, play a few lines, show the high score screen.
  *"800 lines of BASIC. Menus, high scores, level progression. Compiled through the same pipeline as everything you just saw."*

- **5:30 – 6:00**: **Chess in BASIC** or **Frogger in BASIC** — quick 20-second clip showing another BASIC game.
  *"Same chess AI, different language. That's the power of the thin-waist architecture."*

---

### ACT 5: QUICK PEEK UNDER THE HOOD (6:00 – 7:30)

**Goal:** Give just enough technical depth to impress, without losing the audience. Keep it visual.

- **6:00 – 6:30**: Show IL output from a small program on screen. Don't walk through every line — just gesture at it:
  *"Every program compiles to Viper IL — a typed, SSA-style intermediate language with 82 opcodes. This is where both languages meet."*

- **6:30 – 7:00**: Flash the optimizer pass list on screen (constant folding, DCE, inlining, LICM, GVN, SCCP, mem2reg…).
  *"14 optimization passes. A verifier runs after every single one."*

- **7:00 – 7:30**: Show a native binary being produced + mention the custom linker.
  *"Viper generates native machine code — x86 and ARM64 — with its own register allocator, its own linker, and DWARF debug info. The output is a standalone binary with zero runtime dependencies."*

---

### ACT 6: THE MIC DROP — SQL DATABASE (7:30 – 8:30)

**Goal:** End with the most ambitious demo to leave the audience stunned.

- **7:30 – 8:00**: *"But the most ambitious thing built on Viper isn't a game."*
  Launch the SQL database. Run a few queries in the REPL — CREATE TABLE, INSERT, SELECT with a WHERE clause.

- **8:00 – 8:30**: *"This is a working SQL database — 60,000 lines of Zia. MVCC, write-ahead logging, B-tree indexes. Running on a compiler I built, querying a database I built."*
  Let that sink in for a beat.

---

### ACT 7: CLOSE (8:30 – 9:30)

**Goal:** Recap scale, share vision, call to action.

- **8:30 – 9:00**: Stats on screen:
  - 2 language frontends
  - 82 IL opcodes, 14 optimizer passes
  - 2 native backends (x86_64 + ARM64)
  - 226 runtime classes
  - 1,279 tests
  - **0 external dependencies**
  *"Every line — from the parser to the register allocator to the TLS handshake — built from scratch."*

- **9:00 – 9:30**: Vision + CTA
  *"Viper is still growing — more optimizations, a debugger, more language features. If you want to see how a real compiler toolchain works from the ground up, check it out."*
  GitHub link on screen. End card.

---

## Production Notes

### Screen Recordings Needed
1. Graphics-show demo (fireworks, matrix rain, starfield) — cold open
2. Zia code snippets (4 prepared, syntax highlighted) — language tour
3. Chess game (play 2-3 moves) — first live demo
4. Pac-Man, sidescroller, Paint, Centipede — montage clips
5. Side-by-side Zia/BASIC comparison — one short program
6. vTRIS in terminal — BASIC demo
7. Chess-basic or Frogger-basic — quick clip
8. IL text output in terminal — brief flash
9. Native binary compilation in terminal — brief flash
10. SQL database REPL — finale demo

### Thumbnail Ideas
- Chess or Pac-Man screenshot + "I Built a Compiler From Scratch"
- Split screen of game running + IL code + "Zero Dependencies"

### Title Ideas
- "I Built an Entire Compiler From Scratch — Here's What It Can Do"
- "Zero Dependencies: I Built 2 Languages, 226 Classes, and a SQL Database"
- "I Built a Programming Language and Made Games With It"

### Pacing Notes
- No section should feel like a lecture. If you're talking without a visual changing for >15 seconds, cut faster.
- The montage (Act 3) should feel energetic — quick cuts, upbeat music underneath.
- The SQL database reveal (Act 6) should feel like a gear shift — slow down, let it breathe.
- The "zero dependencies" line should recur 3 times: cold open, runtime mention, and closing stats.
