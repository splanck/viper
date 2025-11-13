REM SHARED acceptance and module variable access
DIM g%
g% = 1

SUB UseShared()
  SHARED g%
  ' Acceptance-only: no mutation; just assert SHARED parses
END SUB

UseShared()
PRINT g%
