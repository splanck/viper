# Visual Effects
> ParticleEmitter, ScreenFX

**Part of [Viper Runtime Library](../README.md) › [Game Utilities](README.md)**

---

## Viper.Game.ParticleEmitter

Simple particle system for visual effects like explosions, sparks, smoke, and other game effects.

**Type:** Instance class (requires `New(maxParticles)`)

### Constructor

| Method              | Signature                | Description                        |
|---------------------|--------------------------|------------------------------------|
| `New(maxParticles)` | `ParticleEmitter(Int)`   | Create emitter (max 1024 particles)|

### Properties

| Property     | Type                   | Description                              |
|--------------|------------------------|------------------------------------------|
| `X`          | `Double` (read-only)   | Emitter X position                       |
| `Y`          | `Double` (read-only)   | Emitter Y position                       |
| `Rate`       | `Double` (read/write)  | Particles emitted per frame              |
| `IsEmitting` | `Boolean` (read-only)  | True if currently emitting               |
| `Count`      | `Integer` (read-only)  | Number of active particles               |
| `Color`      | `Integer` (read/write) | Particle color (0xAARRGGBB)              |
| `FadeOut`    | `Boolean` (read/write) | Enable alpha fade over lifetime          |
| `Shrink`     | `Boolean` (read/write) | Enable size reduction over lifetime      |

### Methods

| Method                                      | Signature                      | Description                          |
|---------------------------------------------|--------------------------------|--------------------------------------|
| `Burst(count)`                              | `Void(Integer)`                | Emit burst of particles instantly    |
| `Clear()`                                   | `Void()`                       | Remove all particles                 |
| `Get(index, xPtr, yPtr, sizePtr, alphaPtr)` | `Boolean(Int,Ptr,Ptr,Ptr,Ptr)` | Get particle data by index           |
| `SetGravity(gx, gy)`                        | `Void(Double,Double)`          | Set gravity (per frame²)             |
| `SetLifetime(min, max)`                     | `Void(Int,Int)`                | Set particle lifetime range (frames) |
| `SetPosition(x, y)`                         | `Void(Double,Double)`          | Set emitter position                 |
| `SetSize(min, max)`                         | `Void(Double,Double)`          | Set particle size range              |
| `SetVelocity(minSpd,maxSpd,minAng,maxAng)`  | `Void(Dbl,Dbl,Dbl,Dbl)`        | Set speed and angle ranges           |
| `Start()`                                   | `Void()`                       | Begin continuous emission            |
| `Stop()`                                    | `Void()`                       | Stop emission (particles continue)   |
| `Update()`                                  | `Void()`                       | Update all particles (call per frame)|

### Zia Example

```rust
module ParticleDemo;

bind Viper.Terminal;
bind Viper.Game.ParticleEmitter as PE;
bind Viper.Fmt as Fmt;

func start() {
    var pe = PE.New(200);
    pe.SetPosition(400.0, 300.0);
    pe.SetLifetime(20, 40);
    pe.SetVelocity(2.0, 8.0, 0.0, 360.0);
    pe.SetGravity(0.0, 0.1);
    pe.SetSize(3.0, 6.0);

    // One-shot burst for explosion
    pe.Burst(50);
    pe.Update();
    Say("After burst: " + Fmt.Int(pe.get_Count()));

    // Continuous emission
    pe.set_Rate(5.0);
    pe.Start();
    var i = 0;
    while i < 10 {
        pe.Update();
        i = i + 1;
    }
    Say("After emitting: " + Fmt.Int(pe.get_Count()));

    pe.Stop();
    pe.Clear();
    Say("After clear: " + Fmt.Int(pe.get_Count()));
}
```

### Example: Explosion Effect

```basic
DIM explosion AS OBJECT = Viper.Game.ParticleEmitter.New(200)
explosion.SetPosition(400.0, 300.0)
explosion.SetLifetime(20, 40)
explosion.SetVelocity(2.0, 8.0, 0.0, 360.0)  ' All directions
explosion.SetGravity(0.0, 0.1)
explosion.Color = &HFFFF6600  ' Orange
explosion.SetSize(3.0, 6.0)
explosion.FadeOut = 1
explosion.Shrink = 1
explosion.Burst(100)  ' One-shot burst

' In game loop
explosion.Update()
' Render particles using Get() method
```

---

## Viper.Game.ScreenFX

Screen effects manager for camera shake, color flash, and fade effects.

**Type:** Instance class (requires `New()`)

### Constructor

| Method  | Signature    | Description                    |
|---------|--------------|--------------------------------|
| `New()` | `ScreenFX()` | Create a new effects manager   |

### Properties

| Property       | Type                  | Description                              |
|----------------|-----------------------|------------------------------------------|
| `IsActive`     | `Boolean` (read-only) | True if any effect is active             |
| `ShakeX`       | `Integer` (read-only) | Current X shake offset (fixed-point)     |
| `ShakeY`       | `Integer` (read-only) | Current Y shake offset (fixed-point)     |
| `OverlayColor` | `Integer` (read-only) | Current overlay color (RGB)              |
| `OverlayAlpha` | `Integer` (read-only) | Current overlay alpha (0-255)            |

### Methods

| Method                         | Signature               | Description                              |
|--------------------------------|-------------------------|------------------------------------------|
| `CancelAll()`                  | `Void()`                | Cancel all effects                       |
| `CancelType(type)`             | `Void(Integer)`         | Cancel effects of specific type          |
| `FadeIn(color, duration)`      | `Void(Integer,Integer)` | Fade from color to clear                 |
| `FadeOut(color, duration)`     | `Void(Integer,Integer)` | Fade from clear to color                 |
| `Flash(color, duration)`       | `Void(Integer,Integer)` | Start color flash effect                 |
| `IsTypeActive(type)`           | `Boolean(Integer)`      | Check if effect type is active           |
| `Shake(intensity, dur, decay)` | `Void(Int,Int,Int)`     | Start camera shake effect                |
| `Update(dt)`                   | `Void(Integer)`         | Update effects (dt in milliseconds)      |

### Effect Types

| Constant            | Value | Description              |
|---------------------|-------|--------------------------|
| `SCREENFX_NONE`     | 0     | No effect                |
| `SCREENFX_SHAKE`    | 1     | Camera shake             |
| `SCREENFX_FLASH`    | 2     | Color flash              |
| `SCREENFX_FADE_IN`  | 3     | Fade from color to clear |
| `SCREENFX_FADE_OUT` | 4     | Fade from clear to color |

### Zia Example

```rust
module ScreenFXDemo;

bind Viper.Terminal;
bind Viper.Game.ScreenFX as FX;
bind Viper.Fmt as Fmt;

func start() {
    var fx = FX.New();

    // Camera shake
    fx.Shake(5000, 300, 500);
    fx.Update(16);
    Say("Active after shake: " + Fmt.Bool(fx.get_IsActive()));
    Say("ShakeX: " + Fmt.Int(fx.get_ShakeX()));
    Say("ShakeY: " + Fmt.Int(fx.get_ShakeY()));

    // Flash effect
    fx.Flash(16711680, 200);     // Red flash
    fx.Update(16);
    Say("Overlay alpha: " + Fmt.Int(fx.get_OverlayAlpha()));

    // Cancel all
    fx.CancelAll();
    Say("After cancel: " + Fmt.Bool(fx.get_IsActive()));
}
```

### Example: Damage Effects

```basic
DIM fx AS OBJECT = Viper.Game.ScreenFX.New()

' On player damage
SUB OnDamage()
    fx.Shake(5000, 300, 500)        ' Shake for 300ms
    fx.Flash(&HFFFF0000, 200)       ' Red flash for 200ms
END SUB

' On level transition
SUB TransitionToLevel()
    fx.FadeOut(&HFF000000, 500)     ' Fade to black over 500ms
END SUB

' In game loop
fx.Update(16)  ' 16ms per frame at 60fps

' Apply to camera
DIM camOffsetX AS INTEGER = fx.ShakeX / 1000
DIM camOffsetY AS INTEGER = fx.ShakeY / 1000

' Draw overlay
IF fx.OverlayAlpha > 0 THEN
    canvas.BoxFilled(0, 0, 800, 600, fx.OverlayColor OR (fx.OverlayAlpha << 24))
END IF
```

---


## See Also

- [Core Utilities](core.md)
- [Physics & Collision](physics.md)
- [Animation & Movement](animation.md)
- [Game Utilities Overview](README.md)
- [Viper Runtime Library](../README.md)
