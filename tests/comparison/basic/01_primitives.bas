' Test: Primitive Types and Literals
' Tests: integers, floats, strings, booleans

' Integer types
DIM i AS INTEGER
i = 42
PRINT "Integer: "; i

DIM lng AS LONG
lng = 1000000
PRINT "Long: "; lng

' Float types
DIM sng AS SINGLE
sng = 3.14
PRINT "Single: "; sng

DIM dbl AS DOUBLE
dbl = 3.14159265358979
PRINT "Double: "; dbl

' String
DIM s AS STRING
s = "Hello, World!"
PRINT "String: "; s

' Boolean
DIM b AS BOOLEAN
b = TRUE
PRINT "Boolean TRUE: "; b
b = FALSE
PRINT "Boolean FALSE: "; b

' Type suffix notation
DIM x%
x% = 100
PRINT "Integer suffix: "; x%

DIM y#
y# = 2.718
PRINT "Double suffix: "; y#

DIM z$
z$ = "suffix test"
PRINT "String suffix: "; z$

PRINT "=== Primitives test complete ==="
