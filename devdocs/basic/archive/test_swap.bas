REM Test SWAP statement (BUG-011 fix)
PRINT "=== Testing SWAP with Integers ==="
x = 10
y = 20
PRINT "Before SWAP: x = ";
PRINT x
PRINT "Before SWAP: y = ";
PRINT y
SWAP x, y
PRINT "After SWAP: x = ";
PRINT x
PRINT "After SWAP: y = ";
PRINT y

PRINT ""
PRINT "=== Testing SWAP with Strings ==="
DIM a$ AS STRING
DIM b$ AS STRING
a$ = "Hello"
b$ = "World"
PRINT "Before SWAP: a$ = ";
PRINT a$
PRINT "Before SWAP: b$ = ";
PRINT b$
SWAP a$, b$
PRINT "After SWAP: a$ = ";
PRINT a$
PRINT "After SWAP: b$ = ";
PRINT b$

PRINT ""
PRINT "=== Testing SWAP with Array Elements ==="
DIM arr(5)
arr(0) = 100
arr(1) = 200
PRINT "Before SWAP: arr(0) = ";
PRINT arr(0)
PRINT "Before SWAP: arr(1) = ";
PRINT arr(1)
SWAP arr(0), arr(1)
PRINT "After SWAP: arr(0) = ";
PRINT arr(0)
PRINT "After SWAP: arr(1) = ";
PRINT arr(1)

PRINT ""
PRINT "All SWAP tests passed!"
