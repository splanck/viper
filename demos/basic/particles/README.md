# Particle System Demo

A visually impressive particle fountain demonstrating Viper BASIC's OOP capabilities and graphics system.

## Features

- **OOP Design**: Multiple classes across separate files
- **Particle Physics**: Gravity, velocity, bouncing off walls
- **Color Fading**: Particles fade out over their lifetime
- **Multiple Emitters**: Three colored fountains (orange, yellow, red)
- **Particle Recycling**: Dead particles are removed to maintain performance

## Files

| File | Description |
|------|-------------|
| `main.bas` | Entry point, creates canvas and emitters |
| `particle.bas` | `Particle` class with physics and color fading |
| `system.bas` | `ParticleSystem` class manages collection of particles |
| `emitter.bas` | `Emitter` class spawns particles with random velocities |

## Running

**VM Mode** (slower, instant start):
```bash
./build/src/tools/vbasic/vbasic demos/basic/particles/main.bas
```

**Native Mode** (faster, requires compilation):
```bash
# Emit IL
./build/src/tools/vbasic/vbasic demos/basic/particles/main.bas -o /tmp/particles.il

# Compile to native
./build/src/tools/ilc/ilc codegen arm64 /tmp/particles.il -S /tmp/particles.s
as /tmp/particles.s -o /tmp/particles.o
clang++ /tmp/particles.o \
  build/src/runtime/libviper_runtime.a \
  build/lib/libvipergfx.a \
  -framework Cocoa -framework Metal -framework MetalKit -framework QuartzCore \
  -o /tmp/particles

# Run
/tmp/particles
```

## Performance

Native compilation is approximately 20x faster than VM interpretation.

## Technical Notes

- Uses `Viper.Random.NextInt()` for random numbers (avoid `Viper.Random.Next()` in native mode - see bugs)
- Integer division requires explicit `CDBL()` for floating-point results
- `AddFile` order matters - dependencies must be included first
