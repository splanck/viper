REM Test E_NS_003: ambiguous type reference
REM Two namespaces contain "Thing", USING both causes ambiguity

REM USING must come first to satisfy E_NS_005
USING A
USING B

REM Namespaces declared (will be registered in declare pass)
NAMESPACE A
  CLASS Thing
    DIM x AS I64
  END CLASS
END NAMESPACE

NAMESPACE B
  CLASS Thing
    DIM y AS I64
  END CLASS
END NAMESPACE

REM Attempting to use "Thing" without qualification is ambiguous
CLASS MyClass : Thing
  DIM z AS I64
END CLASS
END
