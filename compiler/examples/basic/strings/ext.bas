10 LET F$ = "report.txt"
20 LET P = INSTR(F$, ".")
30 PRINT RIGHT$(F$, LEN(F$) - P)
