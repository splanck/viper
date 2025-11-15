' Test 12: DO loops
DIM i AS INTEGER

' DO WHILE (pre-test)
i = 0
DO WHILE i < 3
    PRINT "DO WHILE: "; i
    i = i + 1
LOOP

' DO UNTIL (pre-test)
i = 0
DO UNTIL i >= 3
    PRINT "DO UNTIL: "; i
    i = i + 1
LOOP

' DO...LOOP WHILE (post-test)
i = 0
DO
    PRINT "DO LOOP WHILE: "; i
    i = i + 1
LOOP WHILE i < 3

PRINT "All DO loops complete"
END
