REM Test DO WHILE with GOSUB
count% = 0
DO WHILE count% < 3
    PRINT "Count: "; count%
    GOSUB Increment
LOOP
PRINT "Done"
END

Increment:
    count% = count% + 1
RETURN
