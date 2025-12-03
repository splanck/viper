# Overload Resolution (Milestone D)

This document specifies the frontend overload resolution used by the BASIC OOP features.

Scope (0.2.x):
- Methods on classes only. Interface calls are matched by slot name/arity as before.
- Property accessors (`get_*/set_*`) participate as ordinary candidates.
- Conversions: only widening numeric (INTEGER→DOUBLE) are permitted implicitly.

## Candidate Collection

Given a target class `C` and a name `N` at a call site:

- Start with the class's declared method `N` when present.
- If the argument count is 0, include `get_N` (synthesized from `PROPERTY`).
- If the argument count is 1, include `set_N`.
- Filter candidates by:
  - Static/instance: static invocations only match static methods; instance calls only match instance methods.
  - Access control: `PRIVATE` methods are only viable when called from the declaring class.

## Ranking and Conversions

For each viable candidate, score parameters positionally:

- Exact match: +2
- Widening numeric (I64→F64): +1
- Otherwise: candidate is not viable (narrowing or incompatible).

The best total score wins. If multiple candidates tie with the best score, the call is ambiguous.

No user-defined conversions, no string/boolean coercions, and no array re-interpretation are attempted.

## Diagnostics

- `E_OVERLOAD_NO_MATCH`: Emitted when the filtered candidate set contains no viable matches after conversions. The message includes the attempted signature.
- `E_OVERLOAD_AMBIGUOUS`: Emitted when multiple candidates achieve the same best score. The message lists fully-qualified signatures of the top candidates.

## Examples

```basic
CLASS C
  FUNCTION Add(x AS INTEGER, y AS INTEGER) AS INTEGER: RETURN x + y: END FUNCTION
  FUNCTION Add(x AS DOUBLE, y AS DOUBLE) AS DOUBLE: RETURN x + y: END FUNCTION
END CLASS

DIM c AS C: c = NEW C()
PRINT c.Add(1, 2)      ' exact -> INTEGER overload
PRINT c.Add(1, 2.0#)   ' widening -> DOUBLE overload
```

Property as overload participant:

```basic
CLASS P
  FUNCTION Value() AS INTEGER: RETURN 1: END FUNCTION
  PROPERTY Value AS INTEGER: GET: RETURN 2: END GET: END PROPERTY
END CLASS

DIM p AS P: p = NEW P()
PRINT p.Value  ' ambiguous: method vs getter (same arity)
```

