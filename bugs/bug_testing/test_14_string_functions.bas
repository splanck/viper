' Test 14: String functions
DIM s AS STRING
DIM result AS STRING
DIM length AS INTEGER

s = "Hello World"

' LEN - string length
length = LEN(s)
PRINT "Length of '"; s; "' is "; length

' LEFT$ - leftmost characters
result = LEFT$(s, 5)
PRINT "LEFT$(s, 5) = '"; result; "'"

' RIGHT$ - rightmost characters
result = RIGHT$(s, 5)
PRINT "RIGHT$(s, 5) = '"; result; "'"

' MID$ - substring
result = MID$(s, 7, 5)
PRINT "MID$(s, 7, 5) = '"; result; "'"

' UCASE$ - uppercase
result = UCASE$(s)
PRINT "UCASE$ = '"; result; "'"

' LCASE$ - lowercase
result = LCASE$(s)
PRINT "LCASE$ = '"; result; "'"

' STR$ - number to string
result = STR$(42)
PRINT "STR$(42) = '"; result; "'"

' VAL - string to number
DIM num AS INTEGER
num = VAL("123")
PRINT "VAL('123') = "; num
END
