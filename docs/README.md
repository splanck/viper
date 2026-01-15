# Viper Documentation

Welcome to the Viper compiler toolchain documentation. This directory contains language references, tutorials, and
implementation guides.

> **Developer resources**: Architecture docs, code maps, and contributor guides are in `/devdocs`.

---

## Quick Links

| Document                                    | Description                                |
|---------------------------------------------|--------------------------------------------|
| [Getting Started](getting-started.md)       | Build, install, and run your first program |
| [Zia Tutorial](zia-getting-started.md)      | Learn Zia by example                       |
| [Zia Reference](zia-reference.md)           | Complete Zia language specification        |
| [BASIC Tutorial](basic-language.md)         | Learn Viper BASIC by example               |
| [Runtime Library](viperlib.md)              | Viper.* classes and methods reference      |
| [IL Guide](il-guide.md)                     | Comprehensive IL specification             |

---

## Language Documentation

### Zia

- **[Zia Tutorial](zia-getting-started.md)** — Learn Zia with hands-on examples
- **[Zia Reference](zia-reference.md)** — Complete language specification

### Viper BASIC

- **[BASIC Tutorial](basic-language.md)** — Learn Viper BASIC with hands-on examples
- **[BASIC Reference](basic-reference.md)** — Complete language specification

### Viper Pascal

> Note: Pascal documentation is in development. See [experimental/](experimental/) for work-in-progress docs.

- **[Pascal Tutorial](experimental/pascal-language.md)** — Learn Viper Pascal with hands-on examples
- **[Pascal Reference](experimental/pascal-reference.md)** — Complete language specification (WIP)

### Intermediate Language (IL)

- **[IL Guide](il-guide.md)** — Comprehensive guide to Viper IL
- **[IL Quickstart](il-quickstart.md)** — Fast introduction for developers
- **[IL Reference](il-reference.md)** — Complete opcode and type reference

---

## Implementation Guides

### Frontends

- **[Frontend How-To](frontend-howto.md)** — Build your own language frontend

### Virtual Machine

- **[VM Architecture](vm.md)** — VM design, dispatch strategies, and internals
- **[Threading and Globals](threading-and-globals.md)** — VM threading model and process-global state

### Native Code Generation

- **[Backend Guide](backend.md)** — x86-64 and ARM64 code generation (experimental)

---

## Runtime Library

- **[Runtime Library Reference](viperlib.md)** — Complete reference for all `Viper.*` classes, methods, and properties

---

## Additional Resources

- **Examples**: See `/examples` for runnable BASIC and Pascal programs and IL modules
- **Graphics Library**: See [graphics-library.md](graphics-library.md) for the ViperGFX 2D graphics API
- **FAQ**: See [faq.md](faq.md) for frequently asked questions
- **Developer Docs**: See `/devdocs` for architecture and contributor guides
