REM ============================================================================
REM namespace_using.bas
REM Purpose: Demonstrate USING directive for namespace imports.
REM Track A: USING must appear before namespace/class declarations
REM ============================================================================

NAMESPACE Collections
  CLASS List
    DIM size AS I64
  END CLASS
END NAMESPACE

NAMESPACE Utils
  CLASS Helper
    DIM id AS I64
  END CLASS
END NAMESPACE

REM In a multi-file program, another file could use:
REM   USING Collections
REM   USING Utils

PRINT "USING directive example compiled successfully"
END
