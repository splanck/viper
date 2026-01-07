---
status: active
audience: public
last-updated: 2025-11-20
---

# Example: Deterministic Disposal Pattern

This example demonstrates a simple RAII‑like pattern in Viper BASIC using `DISPOSE` to ensure cleanup.

```basic
CLASS Handle
  DESTRUCTOR
    PRINT "closed"
  END DESTRUCTOR
END CLASS

SUB Use()
  DIM h AS Handle
  LET h = NEW Handle()

  ' ... work with h ...

  ' Ensure deterministic cleanup at this point
  DISPOSE h
END SUB

CALL Use()
END
```

Notes:

- If your code has multiple exit points, prefer a single cleanup section before the procedure returns:

```basic
SUB UseManyPaths()
  DIM h AS Handle
  LET h = NEW Handle()

  IF condition THEN
    ' early exit
    GOTO cleanup
  END IF

  ' ... more work ...

cleanup:
  DISPOSE h
END SUB
```

- Static destructors are suitable for process‑wide resources that must outlive all procedures in the module.

