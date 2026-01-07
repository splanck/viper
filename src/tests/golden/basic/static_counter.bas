REM BUG-010 repro: STATIC persists across calls
SUB Counter()
  STATIC c%
  c% = c% + 1
  PRINT c%
END SUB

Counter()
Counter()
Counter()

