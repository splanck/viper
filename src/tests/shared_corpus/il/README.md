# Shared IL Corpus

This directory contains small, deterministic IL programs consumed by multiple
execution and backend parity suites. Programs avoid time, randomness, host file
system state, and unbounded loops so they are suitable for VM, bytecode, and
codegen tests on every host.

`success/` programs must return a stable `i64` value and may write stable
runtime stdout. `traps/` programs intentionally terminate with one of the
shared VM trap kinds.
