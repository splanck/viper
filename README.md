# Viper Monorepo

This repository contains two related projects:

## Projects

### [compiler/](compiler/) — Viper Compiler Toolchain

An IL-first compiler toolchain and virtual machine for exploring intermediate language design, multi-frontend architectures, and interpreter implementation.

- **Frontends:** BASIC, Pascal, ViperLang
- **IL:** Strongly typed, SSA-inspired intermediate language
- **Backends:** VM interpreter, x86-64 native codegen

### [os/](os/) — ViperOS

A capability-based microkernel operating system for AArch64.

- **Architecture:** AArch64 (Raspberry Pi, QEMU)
- **Features:** Microkernel, capability-based security, graphical console

## Repository Structure

```
/
├── compiler/         # Viper compiler toolchain
│   ├── src/          # Source code
│   ├── docs/         # Documentation
│   ├── examples/     # Example programs
│   └── ...
├── os/               # ViperOS operating system
│   ├── kernel/       # Kernel source
│   ├── user/         # User space programs
│   ├── vboot/        # Bootloader
│   └── docs/         # OS documentation
└── README.md         # This file
```

## License

GPL-3.0
