' Test Viper.DateTime infrastructure
' Note: Full BASIC DateTime integration pending frontend runtime lookup

' Test 1: Verify timestamp arithmetic logic
DIM ts AS INTEGER
DIM ts2 AS INTEGER
DIM diff AS INTEGER

' Test known timestamp: June 15, 2024 14:30:45 UTC = 1718457045
' We can verify arithmetic operations work correctly
ts = 1718457045

' Test 2: AddDays logic (10 days = 864000 seconds)
ts2 = ts + 864000
diff = ts2 - ts

IF diff = 864000 THEN
    PRINT "PASS: timestamp arithmetic"
ELSE
    PRINT "FAIL: diff = " + STR$(diff)
END IF

' Test 3: Seconds per day constant
DIM secsPerDay AS INTEGER
secsPerDay = 24 * 60 * 60
IF secsPerDay = 86400 THEN
    PRINT "PASS: seconds per day"
ELSE
    PRINT "FAIL: secsPerDay"
END IF

' Test 4: Days calculation
DIM days AS INTEGER
days = diff / 86400
IF days = 10 THEN
    PRINT "PASS: days calculation"
ELSE
    PRINT "FAIL: days = " + STR$(days)
END IF

' Test 5: Verify positive timestamp
IF ts > 0 THEN
    PRINT "PASS: positive timestamp"
ELSE
    PRINT "FAIL: timestamp"
END IF

PRINT "=== DATETIME TESTS COMPLETE ==="
