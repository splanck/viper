# Plan 25 — `Dialogue3D`: Speaker Anchors, Subtitles, Localization, Choices

## 1. Objective & scope

Wire the existing pieces (2D `Viper.Game.Dialogue` typewriter/queue, GameUI widgets, `Viper.Localization`, plan-24 dialogue voices/ducking, plan-09 subtitle track) into one 3D conversation surface: dialogue boxes/subtitles over the 3D overlay, speaker name/position anchoring to entities, localization-keyed lines, voice-clip sync, and branching choice lists.

**In scope:** (a) `Viper.Game3D.Dialogue3D` wrapper; (b) `Camera3D.WorldToScreen` projection helper; (c) localization-keyed lines + voice clips; (d) choice prompts; (e) subtitle-mode used by Timeline3D.
**Out of scope:** dialogue *authoring* format/graphs (game data; the queue API is the runtime surface), portrait rendering, lip-sync (plan 26).

**Zero external dependencies — absolute.**

## 2. Current state (verified anchors)

- **2D dialogue is canvas-drawn typewriter + queue:** `rt_dialogue.c/h` — "Typewriter text reveal system for RPGs… dialogue queue, and immediate-mode Canvas drawing"; `rt_dialogue_draw(dlg, canvas)` draws to a 2D canvas handle. The 3D overlay pass draws via Canvas3D overlay primitives, so `Dialogue3D` re-implements *draw* against the overlay text/rect path while reusing the reveal/queue *state machine* (refactor: split `rt_dialogue.c` state core from its draw so both canvases share logic — verify split feasibility at write time).
- **Overlay primitives:** final-overlay text + `DrawRect2DAlpha` panels post-post-FX (`game3d.md` §Debug3D, §GameBase3D fade); GameUI widget set incl. `MenuList` keyboard/pad navigation (`docs/viperlib/game/ui.md` §MenuList) and Canvas3D GameUI bridge (`rt_canvas3d_gameui.c`).
- **No world→screen helper:** `rt_camera3d_screen_to_ray` exists; the inverse doesn't (verified fn list) — nameplates/anchors need `WorldToScreen(pos) -> (x, y, visible)`.
- **Localization:** `src/runtime/localization/` runtime (string tables by key/locale — confirm exact API names at write time from `docs/viperlib/localization` docs).
- **Voice + ducking:** plan 24's `Sound3D.playDialogue(clip)` routes/tags/ducks.
- **Timeline:** plan 09's `addSubtitle` renders raw text; this plan upgrades it to keys.

## 3. Design

### 3.1 Core object

New C `src/runtime/graphics/3d/rt_game3d_dialogue.c`. `Dialogue3D.New(world)` — one active conversation per world (second `New` allowed; `show` on one at a time):

- **Lines:** `say(speakerName, textOrKey)` queues a line; `sayVoiced(speaker, textOrKey, clip)` also plays the clip via `playDialogue` and auto-advances when both the reveal finishes and the clip ends (`+ setAutoAdvance(bool)`; manual advance via `advance()` — game binds it to the interact key). Text resolution: if a localization table is bound (`setLocale(table)`) and the string resolves as a key, the localized string is used; otherwise the literal (single rule, documented).
- **Typewriter:** reuse the reveal core (chars/sec `setRevealSpeed`, `skipReveal()` completes the line — the two-stage skip convention: first press reveals, second advances).
- **Speaker anchor:** `setSpeakerEntity(entity)` — the box renders bottom-centered by default; anchored mode (`setAnchored(true)`) positions a compact bubble above the entity via `WorldToScreen(entityPos + headOffset)`, hidden when `visible=false` (behind camera) with fallback to the bottom panel.
- **Choices:** `askChoice(seq<str textOrKeys>)` presents a `MenuList`-backed list (pad/keyboard/mouse via GameUI focus); `choiceMade()` one-shot + `lastChoice()` index. Blocks line advance until chosen.
- **Rendering:** drawn in the world overlay hook (after user overlay, before timeline letterbox — ordering with plan 09 defined: letterbox outermost). Style knobs minimal: panel alpha, text scale, name color (`setStyle(...)`); full theming stays game-side via the raw values.

### 3.2 `Camera3D.WorldToScreen`

`rt_camera3d_world_to_screen(camera, x, y, z, out_sx, out_sy) -> i1 visible` — projects through the render projection (reversed-Z aware, `rt_camera3d_get_render_projection`), returns pixel coordinates + front-of-camera visibility. Generally useful (plan 28 markers reuse it).

### 3.3 Timeline integration

Plan 09's subtitle track calls into the same renderer (subtitle mode = anchored-off, bottom-band, no panel) and accepts keys through the same resolution rule. `Dialogue3D` conversations and timeline subtitles are mutually exclusive on screen (timeline wins; conversation pauses) — documented.

## 4. Implementation steps

1. `WorldToScreen` on Camera3D + unit tests (center/edge/behind cases, reversed-Z correctness vs `screen_to_ray` round-trip).
2. Split the 2D dialogue reveal/queue core from its draw (pure refactor; 2D tests stay green).
3. `Dialogue3D` bottom-panel mode: queue/reveal/advance/skip over overlay draw; C + Zia tests.
4. Voiced lines + auto-advance on clip end (voice-finished query via the existing voice API).
5. Anchored bubbles via `WorldToScreen` + fallback.
6. Choices on GameUI `MenuList` + focus routing.
7. Localization binding + key-resolution rule; Timeline3D subtitle upgrade.
8. runtime.def + audits + ADR + docs (`game3d.md` new §Dialogue, cross-refs to plan 24/09).
9. Zia probe `g3d_test_game3d_dialogue_probe`: two-line voiced conversation + a choice, synthetic input advance, structural HUD asserts on captures, deterministic replay.

## 5. Public API changes (runtime.def)

```
Camera3D: RT_METHOD("WorldToScreen","i1(obj,f64,f64,f64)",…) + a Vec2-returning overload per Math conventions
RT_CLASS_BEGIN("Viper.Game3D.Dialogue3D", Game3DDialogue3D, "obj", Game3DDialogueNew)   /* New(world) */
    RT_METHOD("say","obj(obj,str,str)",…) RT_METHOD("sayVoiced","obj(obj,str,str,obj)",…)
    RT_METHOD("askChoice","obj(obj,obj)",…)      /* seq<str> */
    RT_METHOD("advance","void(obj)",…) RT_METHOD("skipReveal","void(obj)",…)
    RT_METHOD("show","void(obj)",…) RT_METHOD("hide","void(obj)",…)
    RT_PROP("active","i1",get) RT_PROP("lineCount","i64",get)
    RT_METHOD("choiceMade","i1(obj)",…) RT_METHOD("lastChoice","i64(obj)",…)
    RT_METHOD("setSpeakerEntity","void(obj,obj<Viper.Game3D.Entity3D>)",…)
    RT_METHOD("setAnchored","void(obj,i1)",…) RT_METHOD("setAutoAdvance","void(obj,i1)",…)
    RT_METHOD("setRevealSpeed","void(obj,f64)",…) RT_METHOD("setLocale","void(obj,obj)",…)
    RT_METHOD("setStyle","void(obj,f64,f64,f64,f64,f64)",…)
RT_CLASS_END()
```

Leaf `Dialogue3D` unique (2D leaf is `Dialogue`). New file → source-health; ADR `00xx-game3d-dialogue.md`.

## 6. Tests

- **WorldToScreen (C unit):** known camera/point pairs project to expected pixels (±0.5 px); behind-camera returns visible=false; round-trip with `screen_to_ray` within tolerance (fail-before: no API).
- **Reveal/advance:** two-stage skip; queue order; auto-advance fires only when reveal AND clip complete.
- **Anchored:** bubble follows a moving entity across frames (capture-position asserts); off-screen falls back to the bottom panel.
- **Choices:** synthetic pad input navigates and selects index 1; `choiceMade` one-shot; advance blocked until selection.
- **Localization:** bound table resolves keys; unbound leaves literals; missing key uses literal (no trap).
- **2D regression:** existing `Viper.Game.Dialogue` tests green after the core split.

## 7. Verification gates

Full build + ctest incl. 2D dialogue + GameUI suites; `-L graphics3d`; surface audits; `-L slow`. Depends on plan 24 for voiced ducking (soft — voiced lines work without ducking if 24 lands later).

## 8. Risks & constraints

- **The 2D core split** is the risky refactor: `rt_dialogue.c` (26 KB) mixes state and draw; if the split fights back, v1 fallback is an independent 3D state machine (duplicated ~200 lines) with a follow-up unification note — do not let the refactor block the feature.
- **Overlay ordering contract** (user overlay → dialogue → timeline letterbox) is a semantic freeze; test it with a stacked capture.
- **Choice input focus** must not leak to gameplay (interact key advancing dialogue must not also fire plan-21 interactions — document the input-consumption pattern; `Dialogue3D.active` is the game's gate).
- Text shaping is the existing font path (no RTL/complex shaping) — inherited limitation, noted in docs.
