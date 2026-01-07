REM BUG-030/035 repro: globals accessible inside SUB/FUNCTION
DIM g%

SUB SetG()
  g% = 42
END SUB

SetG()
PRINT g%

