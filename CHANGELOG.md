# Changelog

## 2025-09-24T00:00Z
VM: Added direct-threaded and switch-based dispatch loops; retained function-pointer dispatch for portability.

## 2025-09-23T00:00Z
VM: SwitchI32 now uses memoized dense/sorted/hashed dispatch instead of linear scan.

## 2025-09-12T02:47Z
Initial import of the Viper project with the core scaffolding checked in. Architecture documentation outlines the planned IL, VM, and codegen layers, and an early test harness is wired up to keep future changes honest. Most subsystems remain skeletal but provide a foundation for incremental development.

## 2025-09-12T03:23Z
A beginner-friendly IL Quickstart tutorial was added. It introduces core IL concepts with runnable snippets and links from the overview and IL reference so newcomers know where to start.

## 2025-09-12T03:41:12Z
Documentation previously spread across `/docs/reference` and `/docs/references` now resides directly under `/docs`. The two BASIC reference guides were merged into `/docs/basic-language.md` for a single canonical source. All links were updated to the new paths to avoid breakage and the old directories were removed.
