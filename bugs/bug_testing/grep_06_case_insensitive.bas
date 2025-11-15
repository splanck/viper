' Grep Clone Test 06: Case-insensitive search
DIM text AS STRING
DIM pattern AS STRING
DIM textLower AS STRING
DIM patternLower AS STRING
DIM pos AS INTEGER

text = "Hello WORLD and world"
pattern = "world"

PRINT "Original text: "; text
PRINT "Pattern: "; pattern
PRINT ""

' Case-sensitive search
pos = INSTR(text, pattern)
PRINT "Case-sensitive position: "; pos

' Case-insensitive search
textLower = LCASE$(text)
patternLower = LCASE$(pattern)
pos = INSTR(textLower, patternLower)
PRINT "Case-insensitive position: "; pos

PRINT ""
PRINT "Lowercase text: "; textLower
PRINT "Lowercase pattern: "; patternLower

END
