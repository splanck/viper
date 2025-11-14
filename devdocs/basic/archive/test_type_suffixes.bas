REM Test type suffixes
PRINT "=== Testing Type Suffixes ==="

REM String suffix $ (we know this works)
name$ = "Alice"
PRINT "name$ = "; name$

REM Integer suffix % (test if it exists)
count% = 42
PRINT "count% = "; count%

REM Float suffix ! (test if it exists)  
pi! = 3.14159
PRINT "pi! = "; pi!

REM Double suffix # (test if it exists)
e# = 2.71828
PRINT "e# = "; e#

PRINT ""
PRINT "Type suffix tests completed!"
