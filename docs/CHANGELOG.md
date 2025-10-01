---
status: active
audience: public
last-verified: 2025-09-24
---

# Documentation Changelog

## 2025-09-25 — Unified traps and BASIC handlers
- Documented the addition of trap-raising IL opcodes (`trap.*`), structured handler instructions (`eh.*`), and resume forms (`resume.*`).
- Captured BASIC `ON ERROR` / `RESUME` lowering to the new handler and resume primitives.
- Noted the runtime `Err` to trap-kind mapping that now powers consistent diagnostics across the VM and host tools.

## 2025-09-24 — Precise numerics update
- Documented the addition of checked IL arithmetic opcodes (`*.ovf`, `*.chk0`) and checked cast instructions (`cast.*.chk`) introduced with the precise numerics work.
- Recorded that BASIC lowering now emits those checked operations to preserve the new overflow and divide-by-zero traps.
