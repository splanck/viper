---
status: active
audience: public
last-verified: 2025-09-24
---

# Documentation Changelog

## 2025-09-24 — Precise numerics update
- Documented the addition of checked IL arithmetic opcodes (`*.ovf`, `*.chk0`) and checked cast instructions (`cast.*.chk`) introduced with the precise numerics work.
- Recorded that BASIC lowering now emits those checked operations to preserve the new overflow and divide-by-zero traps.
