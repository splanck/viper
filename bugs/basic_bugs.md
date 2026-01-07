# BASIC Bugs

This log captures BASIC frontend defects found during the runtime sweep.

## Format

- **ID**: BASIC-###
- **Component**: parser / sema / lowering / runtime integration
- **Summary**: краткое описание
- **Repro**: BASIC program path + steps
- **Expected**: what should happen
- **Actual**: what happened
- **Notes**: platform, determinism, frequency, workaround

---

## Open Bugs

(Empty)

---

## Closed/Fixed

- **ID**: BASIC-001
  - **Component**: lowering / runtime ctor detection
  - **Summary**: Runtime `.New` methods were treated as constructors even when return type was not `obj` (e.g., `Guid.New`).
  - **Repro**: `tests/runtime_sweep/basic/text.bas` (`Viper.Text.Guid.New` usage)
  - **Expected**: `Guid.New` returns a string without constructor lowering.
  - **Actual**: Lowerer treated it as a constructor, producing mismatched object typing.
  - **Notes**: Fixed by checking runtime method return type before classifying as ctor.

- **ID**: BASIC-002
  - **Component**: lowering / procedure slot typing
  - **Summary**: Object locals lost concrete runtime class metadata during slot resolution.
  - **Repro**: `tests/runtime_sweep/basic/text.bas` (runtime class locals)
  - **Expected**: Object slots retain runtime class typing for correct lowering.
  - **Actual**: Slots treated as generic objects, causing incorrect slot typing in some paths.
  - **Notes**: Fixed by preserving `objectClass` when resolving slot types.

- **ID**: BASIC-003
  - **Component**: golden IL fixtures / runtime signatures
  - **Summary**: Golden IL files expected `Viper.String.IndexOfFrom(i64, str, str)` instead of receiver-first order.
  - **Repro**: `tests/golden/basic_to_il/ex1_hello_cond.il` (`ctest -R basic_to_il_ex1`)
  - **Expected**: `Viper.String.IndexOfFrom(str, i64, str)` matches runtime signature.
  - **Actual**: Golden fixtures declared `IndexOfFrom(i64, str, str)`.
  - **Notes**: Updated golden IL fixtures across basic/IL lowering directories to receiver-first order.
