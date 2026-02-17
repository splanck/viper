---
status: draft
audience: public
last-updated: 2026-02-17
---

# Example: Deterministic Disposal Pattern

> **Note:** The standalone `DISPOSE` statement shown below is not yet implemented as a lexed keyword
> in the BASIC frontend. The pattern is aspirational. For deterministic cleanup today, use `DESTRUCTOR`
> methods and let the runtime call them when the object is freed, or explicitly set the variable to null.

This example demonstrates a RAII‑like pattern in Viper BASIC using `DISPOSE` to ensure cleanup.

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

