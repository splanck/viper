REM Test CONST reassignment protection
CONST MAX = 100
PRINT "MAX = ";
PRINT MAX

REM This should fail with error B2020
MAX = 200
PRINT "This should not print"
