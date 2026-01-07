REM ============================================================================
REM namespace_simple.bas
REM Purpose: Demonstrate basic namespace declaration and usage.
REM Track A: Language-level namespaces - positive flow
REM ============================================================================

NAMESPACE Geometry
  CLASS Point
    DIM x AS I64
    DIM y AS I64
  END CLASS
END NAMESPACE

PRINT "Namespace example compiled successfully"
END
