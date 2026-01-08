# Experimental Features

This directory contains documentation for experimental features that are
not part of the default Viper build.

## Pascal Frontend

The Pascal frontend is experimental and not built by default.

To enable Pascal support:

```bash
cmake -S . -B build -DVIPER_ENABLE_PASCAL=ON
cmake --build build -j
```

This will build:
- `vpascal` - Pascal compiler/runner
- Pascal support in `ilc` (via `ilc front pascal`)
- Pascal test suite

### Documentation

- [Pascal Language Tutorial](pascal-language.md) - Learn Viper Pascal by example
- [Pascal Reference](pascal-reference.md) - Complete Pascal language specification
- [Pascal Specification](ViperPascal_v0_1_Draft6_Specification.md) - Formal language spec

### Developer Documentation

- [Pascal OOP Roadmap](pascal-oop-roadmap.md) - OOP implementation status
- [Pascal Threading Plan](pascal_threading_plan.md) - Threading support roadmap
- [Pascal Frontend Codemap](front-end-pascal.md) - Source code organization

### Demos

Pascal demos are located in `demos/experimental/pascal/`:

| Demo | Description |
|------|-------------|
| `frogger/` | Frogger port to Pascal |
| `centipede/` | Centipede port to Pascal |
| `snake/` | Classic snake game |
| `vtris/` | Tetris port to Pascal |

Run demos (when Pascal is enabled):

```bash
./build/src/tools/vpascal/vpascal demos/experimental/pascal/frogger/frogger.pas
```
