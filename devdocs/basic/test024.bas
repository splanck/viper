x = 1
DO WHILE x <= 10
    PRINT x
    IF x = 5 THEN
        EXIT DO
    END IF
    x = x + 1
LOOP
PRINT "After loop"
