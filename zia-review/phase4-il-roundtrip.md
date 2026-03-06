# Phase 4: IL Roundtrip Verification

**Date:** 2026-03-05
**Method:** Compile `.zia` → IL text (`viper build`) → VM execution (`viper -run`)

---

## Results

| Test File | .zia → direct run | .zia → IL → VM | Status |
|-----------|-------------------|----------------|--------|
| test_primitives.zia | PASS (13/13) | PASS | OK |
| test_operators_arithmetic.zia | PASS (17/17) | PASS | OK |
| test_operators_logical_bitwise.zia | PASS (15/15) | PASS | OK |
| test_control_flow.zia | PASS (16/16) | PASS | OK |
| test_functions.zia | PASS (18/18) | PASS | OK |
| test_lambdas.zia | PASS (15/15) | **FAIL** | BUG-IL-001 |
| test_collections.zia | PASS (23/23) | PASS | OK |
| test_entities.zia | PASS (20/20) | PASS | OK |
| test_inheritance.zia | PASS (17/17) | PASS | OK |
| test_interfaces.zia | PASS (17/17) | **FAIL** | BUG-IL-002 |
| test_values.zia | PASS (21/21) | PASS | OK |
| test_generics.zia | PASS (18/18) | PASS | OK |
| test_optionals.zia | PASS (15/15) | PASS | OK |
| test_match_patterns.zia | PASS (14/14) | PASS | OK |
| test_strings.zia | PASS (13/13) | PASS | OK |
| test_modules.zia | PASS (12/12) | PASS | OK |
| test_type_coercion.zia | PASS (12/12) | PASS | OK |
| test_error_handling.zia | PASS (12/12) | PASS | OK |

**Pass rate:** 16/18 (89%)

---

## IL Roundtrip Bugs

### BUG-IL-001: Lambda Closure IL Serialization — Duplicate Result Name

**File:** test_lambdas.zia
**Error:** `error: line 1350: duplicate result name '%t0'`
**Cause:** When lambdas with closures are lowered to IL, the generated IL text contains
duplicate `%t0` result names in the closure function body. The VM's IL parser rejects
this as a verification error.
**Impact:** Any program using lambdas/closures cannot be serialized to IL text and
re-loaded. Direct execution works fine.
**Severity:** MEDIUM — affects IL text roundtrip only, not runtime execution

### BUG-IL-002: Interface Dispatch IL — Use Before Def

**File:** test_interfaces.zia
**Error:** `error: getShapeName:entry_0: ret %t7: use before def of %7`
**Cause:** The IL emitted for interface dispatch (itable lookup + indirect call) has a
value reference (`%7`) that appears before its definition in the serialized IL text.
The IL printer may be emitting instructions in the wrong order for interface dispatch
sequences.
**Impact:** Programs using interface dispatch cannot roundtrip through IL text. Direct
execution works fine.
**Severity:** MEDIUM — affects IL text roundtrip only, not runtime execution

---

## Notes

- **Native compilation** via `viper build -o` fails for standalone .zia files due to
  missing runtime library linking. This is expected — standalone .zia files are designed
  to be run via `viper run`, not compiled to standalone binaries.
- The direct execution path (`viper run file.zia`) works correctly for all 18 test files
  (288 total assertions pass).
- IL text emission (`viper build file.zia`) produces valid IL for 16/18 files.
