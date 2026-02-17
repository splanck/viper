---
title: VM Periodic Interrupts (Polling)
status: active
audience: public
last-verified: 2026-02-17
---

# Periodic Interrupts and Host Polling

The VM can call back into a host‑provided function every N instructions. This
is useful for integrating UI loops, cancellation, or cooperative multitasking.

## Configuration

Set the cadence via:

- `RunConfig.interruptEveryN` (preferred in‑process), or
- Environment: `VIPER_INTERRUPT_EVERY_N=<N>` (fallback when config is 0).

Provide a callback:

```c++
il::vm::RunConfig cfg;
cfg.interruptEveryN = 1000; // poll every 1k instructions
cfg.pollCallback = [](il::vm::VM &vm) {
  (void)vm; // query state if needed
  return true; // return false to request a pause
};
il::vm::Runner r(module, cfg);
auto status = r.continueRun();
if (status == il::vm::Runner::RunStatus::Paused) {
  // resume later with r.continueRun();
}
```

Overhead is negligible when `interruptEveryN` is zero (fast path).

