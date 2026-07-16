# XENOSCAPE — Part A Visual Overhaul (Implementation Plan & Handoff)

> **STATUS: IMPLEMENTED (2026-05-28) — 13 of 14 recs done; #9 staged.**
> Part A was validated, then implemented batch-by-batch with a frontend build green at every
> boundary, a native build of the demo, and a launch smoke test (no crash/trap). Baseline SHA
> before edits: `281ad9f00`. Change footprint: 8 files, +495/−74.
> The original review report also lives at
> `~/.claude/plans/review-the-xenoscape-demo-cheeky-garden.md` (outside the repo).
>
> **Done:** #1 parallax menu backdrop, #2 tweened logo reveal, #3 ScreenFX screen transitions,
> #4 logo bloom, #5 eased selection, #6 menu SFX (+menu_back), #7 wired rich victory +
> level-complete screens, #8 cinematic level-complete (score count-up + ParticleEmitter burst),
> #10 living/flickering lights, #11 additive lava bloom, #12 **foundation** (palette colour
> helpers + fixed the `EASE_OUT_ELASTIC`/`EASE_OUT_BOUNCE` latent bug), #13 tile depth shade,
> #14 floating damage/score popups.
> **Staged follow-ups (not done):**
>   - **#9 biome backdrops → Camera parallax** — Large; `camera.zia` already runs a per-biome
>     Perlin cloud-parallax system, so this is a multi-biome rewrite of working rich rendering.
>     Stage biome-by-biome on the existing `GameCamera.cam` + `drawBiomeBackground` path.
>   - **#7 `drawEnhancedPause`** — needs 3 new *functional* pause options (World Map / Save /
>     Controls), i.e. gameplay features beyond the visual overhaul; left on the clean 2-option pause.
>   - **#12 bulk migration** — ~970 inline `Color.RGB` calls (sprites.zia 343, menu.zia 116…) is a
>     separate large refactor; the helper foundation + kill-flash-class sample path is in place.

---

## 0. TL;DR — where to pick up

- **Goal:** Implement **Part A** (recommendations #1–#14) of the XENOSCAPE review: a visual overhaul with a cinematic-atmospheric **menu** as the centerpiece, plus gameplay visual upgrades.
- **Done so far:** Reviewed the demo, verified APIs in `runtime.def` **and against the C headers/impl**, confirmed the build path, and resolved every "RE-VERIFY" caveat (see §0.1). Created 5 tracking tasks. **Wrote zero code.**
- **Next concrete action:** record the **baseline commit SHA** (§9.0), run the **baseline build** (§1) to confirm the demo compiles *before* editing, then start **Batch 0/1** (§7).
- **Hard rules (from CLAUDE.md / memory):** Write ALL code yourself — **never use agents to write code**. Don't commit unless asked. Zero external deps. 100% cross-platform. Bind `Viper.*` namespaces at the top of each file. User is visually impaired → **dark/high-contrast only**, and verification must not rely solely on eyesight (§9).

### 0.1 Validation pass — what changed in this revision (2026-05-28)

This plan was validated end-to-end against `src/il/runtime/runtime.def`, the C headers under `src/runtime/`, and the actual `.zia` sources. Corrections folded in below; the high-impact ones:

1. **`SmoothValue` API was wrong.** It is `New(initial:f64, smoothing:f64)`; `Update()` takes **no argument** (frame-based); there is **no `SetTarget`** — `Target` is a writable property (`sv.Target = x`). Affects #5/#4.
2. **`Color` math is all-`i64`, not float.** `Lerp(c1,c2,t)`/`Brighten(c,amt)`/`Darken(c,amt)`/`FromHSL(h,s,l)` take **integer 0–100 percentages**, not `0.0–1.0` floats. Affects #12/#5 directly.
3. **`Lighting2D` API was wrong** (#10). No `AddPointLight`/`RemoveLight`/`SetLightPosition`/`GetLightmap`/`AmbientLight`. Real surface: `New(maxLights)`, `AddLight(x,y,radius,color,lifetimeFrames)`, `AddTileLight(x,y,radius,color)`, `SetPlayerLight(radius,color)`, `ClearLights()`, `Update()`, `Draw(canvas,camX,camY,pScreenX,pScreenY)`, props `Darkness`/`TintColor`/`LightCount`. Lights are **fire-and-forget with a frame lifetime — no handle to mutate**, so #10 must be reframed (§2/§7).
4. **Timing model was undefined and is the biggest structural risk** — now pinned in new **§6.5**. The menu state (`updateMenu`→`updateTitle`/`drawTitle`) currently receives **no `dt`**; ScreenFX needs ms-dt threaded in.
5. **A `ScreenFX` instance already exists** in `renderer.zia` (`fx = FX.New()` at line 117) but is **dormant during the menu** — reuse it, don't add a second (§7 Batch 1).
6. **Latent bug found:** `config.zia:779-781` has `EASE_OUT_ELASTIC = 16` (no elastic exists; 16 = `IN_BOUNCE`) and `EASE_OUT_BOUNCE = 18` (wrong — out-bounce is **17**; 18 = `IN_OUT_BOUNCE`). Fix while here (§4).
7. **Scope reality checks:** the "~86 hardcoded `Color.RGB`" (#12) is actually **~970** calls (sprites.zia 343, palette.zia 163, menu.zia 116…); `EASE_*` constants (#config) **already exist** (`config.zia:775-781`); `menu_move`/`menu_select` SFX **already exist** (#6 — only `menuBack` is new); `hud.zia` `drawDamageNumber` (#14) is **fully implemented but never called** — #14 is a *wiring* task, not an implementation task.

---

## 1. Build, run & verify

```sh
# Frontend compile check (Zia -> IL). This is the fast inner-loop check.
build/src/tools/viper/viper build examples/games/xenoscape -o /tmp/xeno.il

# Full build of all demos (frontend -> IL -> native arm64 exe), optional smoke-run:
./scripts/build_demos_mac.sh            # build only (default = --skip-run)
./scripts/build_demos_mac.sh --run      # build + launch EACH demo ~5s
```

- `viper` binary: `build/src/tools/viper/viper` (already built; reports `v0.2.6-snapshot`). **VERIFIED.**
- `viper build <target> -o out.il` accepts a directory / `viper.project` and emits IL text without native link — confirmed via `viper --help`. **VERIFIED.**
- Project manifest: `examples/games/xenoscape/viper.project` → `lang zia`, `entry main.zia`. **VERIFIED.**
- Build script: `./scripts/build_demos_mac.sh` exists, is xenoscape-aware (lists `xenoscape:${GAMES_DIR}/xenoscape`), default is build-only, `--run` launches **every** demo ~5s each (slow — prefer the frontend-only check for iteration). CLAUDE.md's generic `./scripts/build_demos.sh` also exists; `build_demos_mac.sh` is the macOS/arm64-specific one and runs the same `viper build <dir> -o` under the hood. Either is valid; this plan uses the mac script. **VERIFIED, no conflict.**
- The MCP tools `zia_check` / `zia_compile` work on a single source string, but XENOSCAPE spans ~30 files with cross-file `bind`s, so **whole-project `viper build` is the real check** — prefer it.
- Build in **batches**, not after every function (per user preference). Build at each batch boundary in §7.

---

## 2. The task: Part A recommendations (#1–#14)

| # | Recommendation | Primary API / anchor | Impact·Effort | Validation note |
|---|----------------|----------------------|---------------|-----------------|
| 1 | Cinematic parallax menu backdrop | `Camera.AddParallax`/`DrawParallax` | High·M | API confirmed; scroll args are **percent** (100 = camera speed) |
| 2 | Tweened logo fade-in | `Tween`+`Easing` | High·S | `EASE_OUT_EXPO = 11` (confirmed) |
| 3 | `ScreenFX` transitions between menu screens | `ScreenFX` (FadeIn/Out, CircleOut, Dissolve) | High·M | **Reuse renderer's existing `fx`**; needs deferred-state machine (§7) |
| 4 | Logo/selection bloom | `Pixels.Blur` + `Renderer2D` additive | Med·S | `Blur` returns a **new** obj; cache it (perf) |
| 5 | Eased selection highlight | `SmoothValue`/`Tween` | Med·S | `SmoothValue.Update()` no-arg; `Target` is a property |
| 6 | Menu sound feedback | existing `Sound` bank | High·S | `menu_move`/`menu_select` **already exist**; only `menuBack` is new |
| 7 | Wire up orphaned rich screens | `menu.zia:744/804/927` | Med·S→M | `drawEnhancedPause` needs a **new 5-option input/state handler** |
| 8 | Cinematic level-complete | `Tween`+`Game.UI.Bar`+`ParticleEmitter` | Med·M | `ParticleEmitter.Draw` returns `i64` |
| 9 | Biome backdrops → parallax stacks | `Camera` parallax | High·L | Stage biome-by-biome; integrate with existing `camera.updateBg(dt)` |
| 10 | Living (flickering) lights | `Lighting2D` re-issue + `Tween` | Med·S | **Reframed:** no per-light handle — vary radius/color on per-frame re-add |
| 11 | Bloom on bright FX | `Pixels.Blur` + additive `Renderer2D` | High·M | **Pick one path** — recommend Pixels.Blur+additive (matches #4) |
| 12 | Color math vs **~970** hardcoded `Color.RGB` | `Color.Lerp/Brighten/Darken/FromHSL` | Med·**L** | Count is ~970 not ~86; `t`/`amt` are **i64 0–100**, not float |
| 13 | Tile-edge depth variety | `Gradient2D` **or** `NineSlice2D` | Med·M | **Pick one** — recommend `Gradient2D`; build at `renderer.zia:123` |
| 14 | Floating damage/score popups | `Tween` (HUD) | Med·S | `drawDamageNumber` exists but is **never called** — wire it up |

---

## 3. Chosen menu design — CINEMATIC ATMOSPHERIC (user-selected)

Mood-first; replaces flat gradient + `drawStarfield` (`menu.zia:360`), static title bounce (`menu.zia:300-307`), and instant screen cuts (`menu.zia:198` + recurring siblings, see §5).

- **4-layer parallax backdrop** (`Camera.AddParallax`): (1) deep-space nebula gradient — `Pixels` filled via `Viper.Math.PerlinNoise.Noise2D`, very slow drift; (2) twinkling starfield (per-star alpha via `Tween`); (3) distant alien ridge silhouette (medium); (4) foreground rocks + lone astronaut silhouette with subtle sway. Advance the menu `Camera` slowly each frame (`camera.Move(...)`) so layers scroll. **Build each layer `Pixels` once at init** (perf, §6.6).
- **Title reveal:** "XENOSCAPE" letters fade+rise staggered via `Tween`+`EASE_OUT_EXPO` (= 11); soft additive bloom behind (blurred `Pixels` + additive `Renderer2D`, blend mode 2). **Pre-blur the title bloom once** — it is static (perf). "the descent" fades in last.
- **Sparse menu:** PLAY / CONTINUE / OPTIONS / QUIT, centered; selected item brightens via `Color.Brighten(c, pct)` or `Color.Lerp(a, b, pct)` (**`pct` is an i64 0–100**); thin underline glides via `SmoothValue` (drive its `Target`, read `Value`/`ValueI64`); soft SFX on move/confirm.
- **Transitions:** select → `ScreenFX.CircleOut`/`FadeOut` into next screen; back → `FadeIn`. Ambient: low-rate `ParticleEmitter` dust; occasional low-alpha `ScreenFX.Flash` (distant lightning). Optional idle attract mode: slow parallax pan + a `Typewriter` lore line.
- **Accessibility:** keep all text high-luminance against the dimmed backdrop.

> **ScreenFX color caveat:** `ScreenFX` colors are packed `0xRRGGBBAA` (alpha in the **least-significant** byte), which DIFFERS from the Canvas/`Color.RGB` `0x00RRGGBB` format. Do **not** pass a `Color.RGB(...)` value straight into `FadeOut`/`Flash` expecting the alpha to behave — build the ScreenFX color explicitly. (Source: `rt_screenfx.h:99-102`.)

---

## 4. VERIFIED API signatures (from `runtime.def` + C headers/impl)

All signatures below were read from `runtime.def` and cross-checked against the C headers/impl. **No remaining "RE-VERIFY" items** — the easing/ScreenFX constant tables are now sourced from the canonical enums.

**Camera — `Viper.Graphics.Camera`** (RT_FUNC table `runtime.def:1654-1682`; **class block `runtime.def:8033-8058`** ← method/prop signatures live here)
- `New(width:i64, height:i64) -> Camera`
- `Follow(x,y)`, `Move(dx,dy)`, `SetCenter(x,y)`, `SmoothFollow(targetX,targetY,speed)`, `SetDeadzone(w,h)` — all `i64`
- props: `X`/`Y`/`Zoom` (read+write `i64`); `Width`/`Height`/`CenterX`/`CenterY`/`ParallaxCount`/`Rotation` (read-only)
- `SetBounds(minX,minY,maxX,maxY)`, `ClearBounds()`
- `AddParallax(pixels:obj, scrollXpct:i64, scrollYpct:i64) -> i64` (layer id). **Scroll args are PERCENT** of camera speed: 100 = moves with camera, 50 = half, 0 = static. (Source: `rt_camera.h:198-204`.)
- `RemoveParallax(id)`, `ClearParallax()`, `DrawParallax(canvas:obj) -> i64` (returns # layers drawn)

**Tween — `Viper.Game.Tween`** (class block `runtime.def:10329-10348`)
- `New()`; props (all read-only): `Value:f64`, `ValueI64:i64`, `IsRunning/IsComplete/IsPaused:i1`, `Progress/Elapsed/Duration:i64`
- `Start(from:f64, to:f64, durationFrames:i64, easingType:i64)`, `StartI64(from:i64, to:i64, durationFrames:i64, easingType:i64)` — **durations are FRAMES** (`rt_tween.h:76`), not ms
- `Update() -> i1` — **NO argument**, advances exactly one frame, returns 1 on the frame it completes (`rt_tween.h:93`)
- `Stop()`, `Reset()`, `Pause()`, `Resume()`
- **statics (confirmed present at 10346-10347):** `LerpI64(from:i64, to:i64, t:f64) -> i64`, `Ease(t:f64, easingType:i64) -> f64`

**ScreenFX — `Viper.Game.ScreenFX`** (class block `runtime.def:12012-12034`)
- `New()`; props (all read-only): `IsActive/IsFinished:i1`, `ShakeX/ShakeY:i64`, `OverlayColor/OverlayAlpha:i64`, `TransitionProgress:i64`
- `Update(dt:i64)` — **dt in milliseconds** (`rt_screenfx.h:76`: "fixed-point seconds, 1000 = 1 second"; numerically = ms). `Draw(canvas:obj, w:i64, h:i64)`
- `Shake(intensity:i64, durationMs:i64, decay:i64)` — **3rd arg is a decay RATE [0,1000]** (0 = no decay, 1000 = instant), NOT ms (`rt_screenfx.h:89-91`). intensity is fixed-point px (1000 = 1px).
- `Flash(color, durationMs)`, `FadeIn(color, durationMs)`, `FadeOut(color, durationMs)` — all `void(i64,i64)`
- `Wipe(direction, color, durationMs)`; `CircleIn(cx,cy,color,durationMs)`, `CircleOut(cx,cy,color,durationMs)`
- `Dissolve(color, durationMs)`, `Pixelate(maxBlock, durationMs)`
- `CancelAll()`, `CancelType(type)`, `IsTypeActive(type) -> i1`
- **Color format: `0xRRGGBBAA` (alpha in LSB)** — see §3 caveat.

**ScreenFX effect-type constants** — ✅ VERIFIED authoritative (`src/runtime/game/rt_screenfx.h:40-51`): NONE=0, SHAKE=1, FLASH=2, FADE_IN=3, FADE_OUT=4, WIPE=5, CIRCLE_IN=6, CIRCLE_OUT=7, DISSOLVE=8, PIXELATE=9. Wipe directions (`rt_screenfx.h:54-57`): LEFT=0, RIGHT=1, UP=2, DOWN=3.

**Easing-type constants** — ✅ VERIFIED authoritative (`src/runtime/game/rt_tween.h:34-55`, `rt_ease_type_t`). This enum has **NO Quart/Circ/Elastic** and is the enum consumed by `Tween.Start`/`StartI64`/`Ease`:
LINEAR=0, IN_QUAD=1, OUT_QUAD=2, IN_OUT_QUAD=3, IN_CUBIC=4, OUT_CUBIC=5, IN_OUT_CUBIC=6, IN_SINE=7, OUT_SINE=8, IN_OUT_SINE=9, IN_EXPO=10, **OUT_EXPO=11**, IN_OUT_EXPO=12, IN_BACK=13, OUT_BACK=14, IN_OUT_BACK=15, IN_BOUNCE=16, **OUT_BOUNCE=17**, IN_OUT_BOUNCE=18 (COUNT=19).
> ⚠️ Do **not** confuse this with the separate `Viper.Math.Easing` class (`runtime.def:11097+`), which exposes *named* methods (InQuart/OutCirc/OutElastic/…) at different indices. The integer `easingType` arg is ONLY the `rt_ease_type_t` value above.
> 🐞 **Latent bug to fix:** `config.zia:780` `EASE_OUT_ELASTIC = 16` → no elastic exists; 16 is `IN_BOUNCE`. `config.zia:781` `EASE_OUT_BOUNCE = 18` → wrong; out-bounce is **17** (18 is `IN_OUT_BOUNCE`). Fix `EASE_OUT_BOUNCE = 17` and remove/remap `EASE_OUT_ELASTIC` (closest springy curve is `OUT_BACK = 14`). Grep for current uses before changing.

**Pixels — `Viper.Graphics.Pixels`** (class block `runtime.def:7879+`)
- `New(w:i64, h:i64)`; `Fill(color:i64)`, `Clear()` ← **needed for parallax/procedural layer authoring (plan previously omitted these)**
- `Blur(radius:i64) -> obj` (returns a **NEW** buffer — does not mutate in place), `Tint(color:i64) -> obj`, `Clone() -> obj`
- `Set(x,y,color)`, `Get(x,y) -> color`, `Scale(newWidth:i64, newHeight:i64) -> obj` (**two dims**, not a single factor)
- `LoadPng(path:str) -> obj` (also `Load`/`LoadBmp`/`LoadJpeg`/`LoadGif`)
- in-place primitives also available for procedural art: `DrawDisc`/`DrawBox`/`DrawLine`/`DrawTriangle`/`FloodFill`/`BlendPixel`/`Resize`/`FlipH`/`FlipV`

**Renderer2D — `Viper.Graphics.Renderer2D`** (class block `runtime.def:8182`)
- `New(maxDrawCalls:i64)`, `Begin()`, `End(canvas:obj)`, `SetTint(c:i64)`, `SetAlpha(a:i64)` (0–255)
- `SetBlendMode(mode:i64)` — ✅ `0=alpha, 1=opaque, 2=additive` (confirmed `rt_graphics2d.h:57-59`)
- `DrawPixels(pixels:obj, x:i64, y:i64)`

**Color — `Viper.Graphics.Color`** (static, class block `runtime.def:7845+`)
- `RGB(r,g,b) -> i64`, `RGBA(r,g,b,a) -> i64`
- `Lerp(c1:i64, c2:i64, t:i64) -> i64` — **`t` is an integer PERCENT 0–100** (clamped), NOT a float
- `Brighten(c:i64, amt:i64) -> i64`, `Darken(c:i64, amt:i64) -> i64` — **`amt` is integer percent 0–100**
- `FromHSL(h:i64, s:i64, l:i64) -> i64` — **all i64** (verify h/s/l ranges in `rt_drawing_advanced.c:1576` before procedural use)
- `GetR/G/B/A(c:i64) -> i64` (also `GetH/GetS/GetL`)

**SmoothValue — `Viper.Game.SmoothValue`** (class block `runtime.def:10370`)
- `New(initial:f64, smoothing:f64)` — **2nd arg is the smoothing factor in `[0.0, 0.999]`** (higher = slower; 0 = snap). NOT "damping".
- `Update()` — **NO argument; frame-based** exponential smoothing (`current = current*smoothing + target*(1-smoothing)`)
- **`Target:f64` is a read+write PROPERTY** — set via `sv.Target = x` (there is **no `SetTarget` method**)
- `Value:f64` (read-only), `ValueI64:i64` (read-only, rounded — use for pixel coords)

**ParticleEmitter — `Viper.Game.ParticleEmitter`** (class block `runtime.def:10383`)
- `New(max:i64)`, `Burst(n:i64)`, `SetPosition(f64,f64)`, `SetLifetime(i64,i64)`, `SetVelocity(f64,f64,f64,f64)`, `SetGravity(f64,f64)`, `Start()`, `Stop()`, `Update()`
- `Draw(canvas:obj) -> i64` (**returns count drawn**, not void); also `DrawAt(obj,i64,i64)`, `DrawToPixels(obj,i64,i64)`

**Lighting2D — `Viper.Game.Lighting2D`** (class block `runtime.def:12138-12148`) — ⚠️ **corrected surface**
- `New(maxLights:i64)`
- props: `Darkness:i64` (0..255, read+write), `TintColor:i64` (read+write), `LightCount:i64` (read-only)
- `SetPlayerLight(radius:i64, color:i64)`, `AddLight(x:i64, y:i64, radius:i64, color:i64, lifetimeFrames:i64)`, `AddTileLight(x:i64, y:i64, radius:i64, color:i64)`, `ClearLights()`, `Update()`, `Draw(canvas:obj, camX:i64, camY:i64, playerScreenX:i64, playerScreenY:i64)`
- **No `AddPointLight`/`RemoveLight`/`SetLightPosition`/`GetLightmap`/`AmbientLight`.** Transient/tile lights are re-issued **every frame** (the demo's `lighting.zia` already does this) — there is no retained light to mutate. (Confirmed against the demo's working calls, `lighting.zia:162-226`.)

**Other classes (constructors are non-trivial — note the args):**
- `Gradient2D` (`runtime.def:8447`): `New(i64,i64,i64)`; `SetColors(i64,i64)`, `SetSteps(i64)`, `Sample(i64)->i64`, `SampleRGBA(i64)->i64`, `FillHorizontal(obj)`, `FillVertical(obj)`
- `NineSlice2D` (`Viper.Graphics`, ctor `runtime.def:1943`): `New(obj,i64,i64,i64,i64)`, `DrawToPixels(obj,i64,i64,i64,i64)`. Also `Viper.Game.UI.HudNineSlice` (ctor `5654`): `New(obj,i64,i64,i64,i64)`, prop `Tint`, `Draw(obj,i64,i64,i64,i64)`
- `Game.UI.GameButton` (ctor `5909`): `New(x,y,w,h,text:str)`; `SetText`/`SetSize`/`SetColors`/`SetTextColors`/`SetBorder`, `Draw(obj,i1)`
- `Material2D` (`runtime.def:8200`): `New()`; props `Tint/Alpha/BlendMode`, `Apply(obj)->obj`. `Shader2D` (`runtime.def:8207`): `New(effect:i64)`; props `Effect/Amount/Color`, `Apply(obj)->obj`
- `PerlinNoise` (`Viper.Math`, `runtime.def:11089`): `New(seed:i64)`; `Noise2D(f64,f64)->f64`, `Noise3D(f64,f64,f64)->f64`. ⚠️ `Octave2D`/`Octave3D` are registered as RT_FUNCs but are **absent from the class method table** — they may NOT be callable as instance methods. Use `Noise2D` for the nebula layer (loop octaves yourself if needed).

---

## 5. Files to modify (with VERIFIED anchors)

| File | Lines | Relevant anchors / role (✅ verified / ✏️ corrected) |
|------|------|--------------------------|
| `menu.zia` | 1212 | `class MenuSystem` @ **83**. `MenuList` field decls **88-91**; `ButtonGroup` fields **84-87**; ctor `init(p: Palette)` **103-167** (the old "fields 88-149" label conflated decls + ctor body). `drawTitle` **273-341** (bounce **300-307**, text/glow **310-315**); `drawStarfield` **360-370**; `drawMenuOption` **345-357** (instant `BoxAlpha` highlight @ **351** — #5 target). **Instant state returns recur at: `updateTitle` 193-202 (the `198` anchor), `updateLevelSelect` 230-233, `updatePause` 387-390, `updateGameOver` 416-419** — #3 must hit ALL of them. `drawLevelComplete` **455-491 is WIRED** (called `game.zia:1093`) — NOT orphaned. **Orphaned (zero callers): `drawVictoryScreen` 744, `drawEnhancedPause` 804, `drawEnhancedLevelComplete` 927.** Live pause = `drawPause` **396** + `updatePause` **377**. Binds **64-72**: Graphics/Input/Text.Fmt/IO/Game/Game.UI + `./config`/`./palette`/`./sprites`. **`Viper.Math` NOT bound** (add for PerlinNoise). Navigation = `MenuList.HandleInput(up,down,confirm)->index`; **fires no SFX** — see §7 Batch 3. |
| `game.zia` | 1499 | God object. Binds **107-138**. Subsystem init + palette injection **266-284** (`pal = new Palette()` @ 267; `menus = new MenuSystem(pal)` @ **279** — palette stored in `menu.zia:103`). `run()` main loop **349-397** (loop body **354-396**; `dt = canvas.DeltaTime` @ **355**; `SetFps(60)` @ 351, `SetDTMax` @ 352). `updateMenu` **480-505** (`updateTitle` 481, `drawTitle` 482 — **takes NO `dt`**; ScreenFX wiring point). `updatePlaying` **554-719**. Kill flash **697-711** (inline `Color.RGB` @ **704, 705, 708**). `drawLevelIntro` def **723-746**, call **714-718**. `renderObj.update(dt)`→`fx.Update(dt)` is called ONLY in gameplay (**603, 996**), never in the menu. |
| `camera.zia` | 1219 | `drawBackground` **189**; `drawGrasslandsBiome` **233**, `drawStationBiome` **269**; `drawBiomeBackground` **984**, `drawBiomeParticles` **1191**. Hand-draws all biomes (#9 target). Binds **70-76**. Already driven by `camera.updateBg(dt)` (called `game.zia:588`) — #9 parallax must integrate with that existing dt path, not a parallel one. |
| `renderer.zia` | 891 | **`ScreenFX` already lives here:** `bind Viper.Game.ScreenFX as FX` @ **80**, `fx = FX.New()` @ **117**, `fx.Update(dt)` @ **232/234**, Fade/Circle wrappers **171-194**. Bevel overlay **built** @ **123-136**, **applied** @ **359-366** (already excludes VINE/CHECKPOINT/LAVA/CLOUD — #13 insertion point). `renderPlaying` **259-319** is the single draw-order authority (bg/parallax 273 → tiles 276 → ambient 279 → pickups 282 → player 286 → enemies 292 → projectiles 295 → particles 298 → HUD 301 → overlay 313) — #11 bloom pass inserts before the screen overlay (313). |
| `palette.zia` | 391 | `class Palette` @ **62**, ~160 `expose Integer` color fields **63-222**, populated in `init()` @ **229**; binds `Viper.Graphics.Color` @ 48. Add derived-color helpers here for #12. |
| `sound.zia` | 521 | `class SoundManager` @ **70** wraps `Viper.Audio.SoundBank` (bind @ 60); `setup(basePath)` @ **100** registers WAVs **110-121** with Synth fallback **125-160**. **`menu_move` + `menu_select` already exist** (`playMenuMove` @ **367**, `playMenuSelect` @ **361**). Only **`menuBack`** is new for #6. `gen_sounds.c` (11 KB) generates 12 WAVs in `sounds/` (no `menu_back.wav` yet). |
| `hud.zia` | 418 | `drawDamageNumber` **368-383** is **fully implemented** (upward float + alpha fade + crit gold/normal white) but has **ZERO callers** — #14 = wire it into damage/score event sites (+ maybe a score variant), not "implement". |
| `lighting.zia` | 249 | `class LightingSystem` @ **144**, bind `Viper.Game` @ 82. `initLighting(p)` @ **160** (`lit = Lighting2D.New(MAX_DYN_LIGHTS)` @ 162, `Darkness`/`TintColor`/`SetPlayerLight` 163-165). Transient `addLight`→`AddLight` @ **190**; tile `drawTileLight`→`AddTileLight` @ **226**; flash helpers **230-248**. #10 reframed (§7). |
| `particles.zia` | 604 | `class ParticleSystem` @ **80**, `emitter = PE.New(256)` @ **89**. Bright-FX bursts: `burstSpark` 151, `burstDeath` 179, `burstEnemyDeath` 236, `burstBossIntro` 312 — #11 bloom attaches at these emit sites. |
| `config.zia` | 832 | `module config` @ 75; 562 module-scope `final`s. **`EASE_*` ALREADY EXIST** @ **775-781** (used by `hud.zia:269`) — extend, don't re-add; **fix the two bugged values (§4).** `TRANS_*_MS` timing block @ **794-801** (`TRANS_FADE_MS=300`, etc.). No `SCREENFX_*` constants yet (genuinely new). Proof `final` holds int literals at module scope: `SCREEN_W=1280` @ 80, `TILE_SIZE=64` @ 82. |

---

## 6. Zia gotchas (from project memory — READ BEFORE CODING)

- **Bind namespaces at top of every file** (e.g. `bind Viper.Graphics;`); never inline `Viper.X.Y.method()`. **Add `bind Viper.Math;` to `menu.zia` if you use `PerlinNoise`** (not currently bound).
- `var items: List[Integer] = [];` (not `List[Integer].New()`).
- Params are `name: Type` (e.g. `func f(a: Integer, b: Integer)`). Range loops: `for i in 0..10` (double-dot).
- `final` **can** hold integer literals at module scope (proof: `config.zia:80`); it **cannot** hold `Color.RGB(...)` (runtime fn) → use class fields / palette object instead. `hide final` is NOT valid inside class bodies — module scope only.
- Class methods must be `expose func` to be callable externally; inherited `init()` with args has an IL codegen bug — use named setup methods like `initX()`.
- Keyword change already in tree: `entity`→`class`, `value`→`struct`.
- Zia emits overflow arithmetic (`IAddOvf` etc.) by default.
- Canvas immediate-mode API: `New(title,w,h)`, `Poll/Flip/Clear/Box/Disc/Ring/Frame/Line/Text` (+ scaled/centered text variants, `DiscAlpha`, `BoxAlpha`, `GradientV/H`). `canvas.DeltaTime` is `i64` **milliseconds** (clamped by `SetDTMax`).
- `#ifdef VIPER_ENABLE_GRAPHICS`: not relevant to Zia game code, but if you ever touch `rt_canvas_*` C stubs, add BOTH real impl and stub.

### 6.5 ⏱️ Timing model (NEW — read before Batch 1)

The demo runs `canvas.SetFps(60)` (`game.zia:351`) and computes `dt = canvas.DeltaTime` (ms, clamped by `SetDTMax`) at `game.zia:355`. Three animation primitives use **different clocks**:

| Primitive | `Update` form | Clock | Durations |
|-----------|---------------|-------|-----------|
| `Tween` | `Update()` (no arg) | **per-frame** | **frames** (≈ ms/16.67 @ 60fps) |
| `SmoothValue` | `Update()` (no arg) | **per-frame** | n/a (smoothing factor) |
| `ScreenFX` | `Update(dt:i64)` | **milliseconds** | **ms** |

**Canonical rule for this overhaul:**
1. `Tween` and `SmoothValue` are **per-frame** — call their `Update()` once per rendered frame; express `Tween` durations in **frames**.
2. `ScreenFX` is **ms-based** — feed it the real `dt = canvas.DeltaTime`; express its durations in **ms** (the `TRANS_*_MS` constants).
3. At a steady 60fps the two clocks track (1 frame ≈ 16ms), but they **desync during lag spikes** (dt is clamped, frames keep ticking 1/frame). Acceptable for a demo; just don't mix units within a single effect.

**Plumbing gap to close FIRST:** `updateMenu` → `updateTitle()`/`drawTitle()` receive **no `dt`**. `ScreenFX.Update(dt)` cannot be driven in the menu until `dt` is threaded in. Batch 1 must add a `dt` parameter to the menu update path (e.g. `menus.tick(dt)` / `menus.updateTitle(dt)`), reading `dt` from `run()`'s loop scope (`game.zia:355`).

### 6.6 🚀 Performance budget (NEW)

`SetFps(60)` = a **16.6 ms/frame** budget. This overhaul stacks per-frame work: 4 parallax layers + per-letter title tweens + additive bloom + dust particles (menu), then biome parallax + FX bloom + flickering lights (gameplay). Guardrails:
- **Build static `Pixels` once at init**, never per frame: parallax layer art, and the **pre-blurred title bloom** (the title is static — blur it once, redraw the cached buffer). `Pixels.Blur` is a CPU blur and returns a new buffer; calling it every frame is the #1 FPS risk.
- Reuse the existing per-frame light re-issue model (`lighting.zia`) — don't add a second lighting pass.
- Make bloom/lighting effects **toggleable** so they can be disabled if FPS drops.
- During `--run` smoke, log `canvas.DeltaTime` and confirm it stays under ~20 ms (≈50 fps floor) on the dev machine (see §9).

---

## 7. Implementation sequence (BATCHES — build at each boundary)

**Batch 0 — Color foundation + constant fixes (#12 foundation) — moved EARLIER**
- Add derived-color helpers to `palette.zia` (`Color.Lerp`/`Brighten`/`Darken` — remember **`t`/`amt` are i64 0–100**, not floats). The menu selection highlight (Batch 3) depends on these, so they must land before Batch 3.
- Fix the two bugged easing constants in `config.zia`: `EASE_OUT_BOUNCE = 17`; remove or remap `EASE_OUT_ELASTIC` (→ `OUT_BACK = 14`). Grep current uses first.
- (Defer the bulk ~970-call `Color.RGB` migration — start with the kill flash `game.zia:704-711` as a sample; the full migration is Large and can trail the menu work.)
- **Build.** ✅ *Done when:* `viper build` clean; helpers callable from a throwaway call in `palette.zia`.

**Batch 1 — Menu animation foundation + logo (#2, #4) + dt plumbing**
- Thread `dt` into the menu update path (§6.5): `updateMenu` reads `dt` (in scope @ `game.zia:355`) and passes it down (`menus.tick(dt)` or `menus.updateTitle(dt)`).
- **ScreenFX ownership decision:** reuse the renderer's existing `fx` (`renderer.zia:117`) rather than creating a second instance. Either (a) call `renderObj.update(dt)` + a new `renderObj.drawScreenFX(canvas)` inside `updateMenu`, **or** (b) if `MenuSystem` owns its own `ScreenFX`, you MUST add explicit `screenFx.Update(dt)` + `screenFx.Draw(canvas, SCREEN_W, SCREEN_H)` calls in the menu path. Document the choice. (Two simultaneously-active ScreenFX instances will double-overlay — avoid.)
- Add `Tween` fields (per-letter or a shared phase) + an anim timer to `MenuSystem`; construct in `init(p: Palette)` (`menu.zia:103`).
- Implement staggered logo fade-in (`Tween`+`EASE_OUT_EXPO` = 11) and additive bloom: render title to `Pixels` **once**, `Blur` it **once** (cache the returned buffer), draw additively via `Renderer2D` (blend mode 2).
- **Build.** ✅ *Done when:* title letters visibly stagger-in over ~N frames; a temporary log of `fx.OverlayAlpha` / tween `Progress` shows animation; FPS log under ~20 ms.

**Batch 2 — Parallax backdrop (#1)**
- `Camera.New(SCREEN_W, SCREEN_H)`; build 3-4 `Pixels` layers **at init** (nebula via `PerlinNoise.Noise2D` + `Pixels.Fill`/`Set`, stars, ridge, foreground); `AddParallax(layer, scrollXpct, scrollYpct)` (**percent** args); `camera.Move(...)` slow drift each frame; `DrawParallax(canvas)` first in `drawTitle`. Retire manual `drawStarfield` scroll math.
- **Build.** ✅ *Done when:* `DrawParallax` return value == number of `AddParallax` calls; layers visibly drift at different rates; no per-frame `Pixels` allocation.

**Batch 3 — Menu interaction (#5, #6, #3)**
- #5: drive the selection-highlight Y with `SmoothValue` (set `sv.Target = targetY` each frame, read `sv.ValueI64`; `Update()` no-arg) instead of the instant `BoxAlpha` @ `menu.zia:351`. Brighten the selected label via `Color.Brighten`/`Lerp` (i64 percent).
- #6: add a `menuBack` sound (extend `gen_sounds.c` for `menu_back.wav` + re-run, or rely on the Synth fallback path); call existing `playMenuMove`/`playMenuSelect` on navigation/confirm. **Move-SFX detection:** `HandleInput` returns an index and fires no event — compare `get_Selected()` before/after the call, or play `menuMove` when `Action.Pressed(ACT_UP) || Action.Pressed(ACT_DOWN)`; play confirm on `chosen >= 0`.
- #3: deferred-transition state machine — on confirm, fire `ScreenFX.FadeOut`/`CircleOut` and store a `pendingState` + timer in `MenuSystem`; do **not** return the new state to `game.zia` immediately. When the transition crosses its midpoint (or `IsTypeActive(FADE_OUT)` clears), return `pendingState`; `FadeIn` on entry. This requires stopping `game.zia:484-504` from switching on the raw `updateTitle()` return, and replacing the instant returns at **193-202 / 230-233 / 387-390 / 416-419**.
- **Build.** ✅ *Done when:* selecting an item plays a fade before the state switch (no instant cut); move/confirm/back SFX audible; highlight glides instead of snapping.

**Batch 4 — Menu screens (#7, #8)**
- #7: wire the orphaned screens. `drawVictoryScreen` (744) and `drawEnhancedLevelComplete` (927) are pure draw fns — swap them in for the live `drawVictory`/`drawLevelComplete` call sites (`game.zia:1093/1115`). **`drawEnhancedPause` (804) is harder:** it renders **5 options** (Resume/World Map/Save/Controls/Quit) with a `selectedOption` param, but the live pause (`drawPause` 396 + `updatePause` 377) only handles 2 — wiring it requires a **new input/state handler** producing `selectedOption`. Budget for that or defer `drawEnhancedPause`.
- #8: cinematic level-complete — `Tween` score count-up (`EASE_OUT_QUAD` = 2), animated `Game.UI.Bar` fill, `ParticleEmitter` burst on the rank letter (remember `Draw` returns `i64`).
- **Build.** ✅ *Done when:* a temporary log/assert confirms the *enhanced* fns are actually entered (compile alone won't catch a dispatch still calling the old `drawPause`).

**Batch 5 — Gameplay visuals (#9, #10, #11, #13, #14)**
- #14 (cheap wire-up): call the existing `hud.zia:drawDamageNumber` from damage/score event sites; add a score-popup variant if wanted.
- #9: convert `camera.zia` biome backdrops to `Camera` parallax layers, **integrating with the existing `camera.updateBg(dt)` path** (`game.zia:588`). **Largest item — time-box: convert ONE biome (Grasslands @ 233) first; if it exceeds the box, ship that biome and defer the rest.**
- #10 (**reframed**): there is no per-light handle. Hold a `Tween`/oscillator in `lighting.zia`, advance it each frame, and feed its value as the **`radius` (or color brightness)** argument when the renderer re-issues tile/transient lights (`AddTileLight`/`addLight` are already called every frame). Or animate `lit.Darkness`/`lit.TintColor` globally, or re-call `SetPlayerLight` with a tweened radius.
- #11 (**pick ONE path — recommend `Pixels.Blur` + additive `Renderer2D`**, matching Batch 1's bloom): bloom pass on lava/explosions/projectiles, inserted in `renderPlaying` before the screen overlay (`renderer.zia:313`).
- #13 (**pick ONE — recommend `Gradient2D`**): per-tile-class depth bevels, built alongside the existing bevel overlay at `renderer.zia:123-136`, applied at 359-366.
- (#12 bulk migration): if pursuing, the heavy files are `sprites.zia` (343), `menu.zia` (116), `camera.zia`/`achievements.zia` (52 each). This is genuinely Large — treat as its own effort, not a Batch-5 line item.
- **Build + `./scripts/build_demos_mac.sh --run` smoke check.**

> Risk note: #9 and the #12 bulk migration are the Large items. If time-boxed, land Batches 0-4 (the menu — the user's stated priority) first, then the cheap wins #14/#10, then #11/#13, then #9 and #12 last.

---

## 8. Task tracker state

Created in the paused session (IDs may reset after restart — recreate if missing):
1. Color/constant foundation: palette helpers + EASE_* fix → **Batch 0** (NEW — front-loaded)
2. Menu infra: dt plumbing + reuse renderer ScreenFX + Tween → Batch 1
3. Menu #1: cinematic parallax backdrop → Batch 2
4. Menu #2/#4: tweened logo fade-in + cached bloom → Batch 1
5. Menu #5/#6: eased selection + sound (menuBack only) → Batch 3
6. Menu #3: ScreenFX deferred-transition state machine → Batch 3
7. Menu #7/#8: wire orphaned screens + cinematic level-complete → Batch 4
8. Gameplay #9/#10/#11/#13/#14 → Batch 5
9. Final: baseline SHA + build + smoke-run + state-log validation

---

## 9. Verification checklist before reporting done

**9.0 Baseline (do FIRST, before any edit):** record the current `git rev-parse HEAD` SHA (and/or branch off it) so any regression can be diff'd/reverted. Do NOT commit unless the user asks — just note the SHA here.

**9.1 Build gates (per batch):**
- [ ] `build/src/tools/viper/viper build examples/games/xenoscape -o /tmp/xeno.il` succeeds (frontend clean) — run at **every** batch boundary.
- [ ] `./scripts/build_demos_mac.sh` builds xenoscape to native exe with no errors (end of Batch 5).
- [ ] `--run` smoke check launches without crash.

**9.2 Eyesight-independent verification (user is visually impaired):**
- [ ] During a short `--run`, log per-frame state to stdout — `titleTimer`, `fx.OverlayAlpha`, `fx.IsActive`, `Camera.ParallaxCount`, selected `Tween.Progress`, and `canvas.DeltaTime` — and confirm the expected transitions occur (parallax count == layers added; overlay alpha ramps on a transition; dt stays < ~20 ms).
- [ ] For Batch 4, a temporary log/assert confirms the *enhanced* screens are actually entered.

**9.3 Behavioral / regression:**
- [ ] Menu shows: parallax drift, logo fade-in, eased selection, deferred screen transitions, SFX (move/confirm/back).
- [ ] No regressions in gameplay rendering (kill flash, HUD, biome backgrounds); existing `EASE_*` users (`hud.zia:269`) still animate correctly after the constant fix.
- [ ] All edits keep `bind` discipline (incl. new `Viper.Math` bind if PerlinNoise used); dark/high-contrast preserved.
- [ ] Performance: bloom/parallax layers built at init (not per frame); bloom/lighting toggleable.
- [ ] Do NOT commit unless the user asks.
