REM Simple array test
CONST MAX = 3

DIM ids(MAX) AS INTEGER
DIM names$(MAX)

REM Initialize
DIM i AS INTEGER
FOR i = 0 TO MAX - 1
    ids(i) = i + 1
    names$(i) = "Person" + STR$(i + 1)
NEXT i

REM Print
FOR i = 0 TO MAX - 1
    PRINT "ID: " + STR$(ids(i)) + " Name: " + names$(i)
NEXT i

END
