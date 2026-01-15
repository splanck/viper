# Viper Demos

Example programs demonstrating Viper BASIC and Zia capabilities.

## BASIC Demos

| Demo | Description | Features |
|------|-------------|----------|
| [Centipede](basic/centipede/) | Classic arcade game | ANSI colors, scoreboard, multiple levels, OOP |
| [Chess](basic/chess/) | Complete chess game with AI | ANSI graphics, AI opponent, full rules |
| [Frogger](basic/frogger/) | Classic road-crossing game | ANSI graphics, cars, trucks, logs, lives |
| [Monopoly](basic/monopoly/) | Board game implementation | 4 players (1 human + 3 AI), properties, trading |
| [Pacman](basic/pacman/) | Classic arcade maze game | OOP classes, terminal graphics, AI ghosts |
| [Particles](basic/particles/) | Graphics particle system | Canvas API, OOP, physics, color fading |
| [vTris](basic/vtris/) | Tetris clone | Levels, high scores, colorful ANSI graphics |

## Zia Demos

| Demo | Description | Features |
|------|-------------|----------|
| [Frogger](zia/frogger.zia) | Frogger in Zia | Entity types, generics, imports |
| [Entities](zia/entities.zia) | Entity system demo | Value/entity types, methods |

## Running Demos

### BASIC (VM Mode)
```bash
./build/src/tools/vbasic/vbasic demos/basic/<demo>/<main>.bas
```

### Zia (VM Mode)
```bash
./build/src/tools/viper/viper demos/zia/<demo>.zia
```

### Native Compilation (BASIC)
```bash
# Emit IL
./build/src/tools/vbasic/vbasic demos/basic/<demo>/<main>.bas -o /tmp/demo.il

# Compile to native (ARM64 macOS)
./build/src/tools/ilc/ilc codegen arm64 /tmp/demo.il -S /tmp/demo.s
as /tmp/demo.s -o /tmp/demo.o
clang++ /tmp/demo.o build/src/runtime/libviper_runtime.a -o /tmp/demo

# For graphics demos, also link:
#   build/lib/libvipergfx.a
#   -framework Cocoa -framework Metal -framework MetalKit -framework QuartzCore

# Run
/tmp/demo
```

## Demo Types

- **Terminal Games**: Use ANSI escape codes for colored text graphics (Centipede, Chess, Frogger, etc.)
- **Canvas Games**: Use `Viper.Graphics.Canvas` for pixel graphics (Particles)

## See Also

- [BASIC Language Reference](../docs/basic-reference.md)
- [Zia Language Reference](../docs/zia-reference.md)
- [Runtime Library](../docs/viperlib/README.md)
