# ViperOS Documentation

This directory contains the complete documentation for ViperOS, a capability-based microkernel operating system for
AArch64.

## Index

- [Documentation Index](index.md)
- [Kernel Narrative Guides](kernel/index.md)

## Contents

### Specification

- [**ViperOS ARM64 Specification**](spec/ViperOS_ARM64_Spec.md) - Complete technical specification (v1.4)

### Implementation Plans

| Phase | Document                                               | Description                   |
|-------|--------------------------------------------------------|-------------------------------|
| 1     | [Graphics Boot](plans/phase1-graphics-boot.md)         | Boot to graphical console     |
| 2     | [Multitasking](plans/phase2-multitasking.md)           | Tasks, scheduler, IPC         |
| 3     | [User Space](plans/phase3-user-space.md)               | Address spaces, capabilities  |
| 4     | [Filesystem & Shell](plans/phase4-filesystem-shell.md) | ViperFS, VFS, vsh             |
| 5     | [Input & Polish](plans/phase5-input-polish.md)         | Keyboard, mouse, line editing |
| 6     | [Networking](plans/phase6-networking.md)               | TCP/IP, DNS, HTTP             |

### Status & Reports

- [**Progress Tracking**](progress.md) - Detailed implementation checklist
- [**Implementation Report**](report.md) - Current status, limitations, next steps

## Quick Links

- **Source Code:** `../kernel/` (kernel), `../user/` (user space)
- **Build Script:** `../build_viper.sh`
- **Disk Image:** `../disk.img`

## Architecture Overview

```
ViperOS Architecture
====================

EL0 (User)     ┌─────────┐  ┌─────────┐  ┌─────────┐
               │  vinit  │  │   vsh   │  │  apps   │
               └────┬────┘  └────┬────┘  └────┬────┘
                    │            │            │
                    └────────────┼────────────┘
                                 │ SVC #0 (syscall)
                    ─────────────┼─────────────
EL1 (Kernel)                     │
               ┌─────────────────┴─────────────────┐
               │          ViperOS Kernel           │
               │  ┌───────┬───────┬───────┬─────┐  │
               │  │Sched  │ IPC   │  VFS  │ Net │  │
               │  └───────┴───────┴───────┴─────┘  │
               │  ┌───────┬───────┬───────┬─────┐  │
               │  │ PMM   │ VMM   │ GIC   │Timer│  │
               │  └───────┴───────┴───────┴─────┘  │
               └─────────────────┬─────────────────┘
                                 │
               ┌─────────────────┴─────────────────┐
               │         virtio-mmio drivers       │
               │    (blk, net, input, ramfb)       │
               └─────────────────┬─────────────────┘
                                 │
               ──────────────────┴──────────────────
Hardware       QEMU virt machine (AArch64)
```

## Color Palette

| Name        | Hex       | RGB           | Usage        |
|-------------|-----------|---------------|--------------|
| Viper Green | `#00AA44` | 0, 170, 68    | Primary text |
| Dark Brown  | `#1A1208` | 26, 18, 8     | Background   |
| Yellow      | `#FFDD00` | 255, 221, 0   | Warnings     |
| White       | `#EEEEEE` | 238, 238, 238 | Bright text  |
| Red         | `#CC3333` | 204, 51, 51   | Errors       |
