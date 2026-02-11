REM BUG-003 regression: NOT operator should always return BOOLEAN
DIM x AS BOOLEAN
DIM y AS INTEGER
x = TRUE
y = 42
PRINT NOT x
PRINT NOT (y > 10)
PRINT NOT FALSE
