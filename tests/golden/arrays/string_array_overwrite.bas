REM BUG-032: Overwrite element should release old and retain new

DIM names$(2)
names$(0) = "A"
names$(0) = "B"

' Use MID$ to materialize rvalues to avoid lvalue retain/release helpers
PRINT MID$(names$(0), 1)

