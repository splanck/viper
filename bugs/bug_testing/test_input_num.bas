REM Test INPUT with numbers only
DIM num1 AS INTEGER
DIM num2 AS SINGLE

PRINT "Enter integer: ";
INPUT num1

PRINT "Enter decimal: ";
INPUT num2

PRINT ""
PRINT "You entered:"
PRINT "Integer: "; num1
PRINT "Decimal: "; num2
PRINT ""

DIM sum AS SINGLE
sum = num1 + num2
PRINT "Sum: "; sum
