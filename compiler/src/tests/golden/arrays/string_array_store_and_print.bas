REM BUG-032/033: String arrays should store and retrieve string elements

DIM names$(3)
names$(0) = "Alice"
names$(1) = "Bob"

REM Use MID$ to materialize rvalues to avoid lvalue retain/release helpers
PRINT MID$(names$(0), 1)
PRINT MID$(names$(1), 1)
