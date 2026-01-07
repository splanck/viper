' Grep Clone Test 02: Test INSTR function
DIM text AS STRING
DIM pattern AS STRING
DIM pos AS INTEGER

text = "Hello World"
pattern = "World"

pos = INSTR(text, pattern)
PRINT "Position: "; pos
END
