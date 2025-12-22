# ViperLang Bugs

This log captures ViperLang language defects found during the runtime sweep.

## Format

- **ID**: VL-###
- **Area**: parser / sema / lowering / runtime integration
- **Summary**: краткое описание
- **Repro**: ViperLang program path + steps
- **Expected**: what should happen
- **Actual**: what happened
- **Notes**: platform, determinism, frequency, workaround

---

## Open Bugs

(Empty)

---

## Closed/Fixed

- **ID**: VL-000
  - **Area**: runtime sweep
  - **Summary**: No ViperLang defects observed in the current sweep.
  - **Repro**: `tests/viperlang_runtime/*`
  - **Expected**: All programs pass.
  - **Actual**: All programs passed.
  - **Notes**: Logged as baseline for the 2025-12-22 sweep.
