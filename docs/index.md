---
status: active
audience: public
last-verified: 2025-11-13
---

# Viper Documentation

Welcome to the Viper compiler toolchain documentation. This directory contains language references, quickstart guides, and implementation guides for working with Viper.

> **For developers**: Architecture docs, code maps, and contributor guides are in `/devdocs`.

---

## Getting Started

**New to Viper?** Start here:

- **[Getting Started](getting-started.md)** — Quick setup guide for building and running the toolchain locally

---

## Language Documentation

### Viper BASIC

Learn the Viper BASIC language and its features:

- **[BASIC Tutorial](basic-language.md)** — Learn Viper BASIC by example with hands-on walkthroughs
- **[BASIC Reference](basic-reference.md)** — Complete language reference for Viper BASIC syntax and semantics

### Intermediate Language (IL)

Understand Viper's intermediate representation:

- **[IL Guide](il-guide.md)** — Comprehensive IL guide covering quickstart, reference, lowering rules, and optimization passes
- **[IL Quickstart](il-quickstart.md)** — Fast-track introduction to Viper IL for developers
- **[IL Reference](il-reference.md)** — Normative specification for Viper IL opcodes, types, and semantics

---

## Implementation Guides

### Frontend Development

Building a new language frontend for Viper:

- **[Frontend How-To](frontend-howto.md)** — Complete implementation guide for writing a Viper frontend in C++

### Virtual Machine

Understanding the Viper VM execution engine:

- **[VM Architecture](vm.md)** — Comprehensive guide to the VM's design, execution model, and source code

### Backend Development

Building native code generators for Viper:

- **[Backend Guide](backend.md)** — Complete guide to the x86-64 backend architecture, code generation pipeline, and register allocation

---

## Additional Resources

- **Examples**: See `/examples` for runnable BASIC programs and IL modules
- **Developer Documentation**: See `/devdocs` for architecture, code maps, and contributor guides
- **Tools**: The `ilc` compiler driver supports multiple frontends and execution modes
