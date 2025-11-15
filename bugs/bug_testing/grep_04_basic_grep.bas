' Grep Clone Test 04: Basic grep functionality
' Search for pattern in file and print matching lines

DIM filename AS STRING
DIM pattern AS STRING
DIM line AS STRING
DIM fileNum AS INTEGER
DIM lineNum AS INTEGER
DIM matchCount AS INTEGER
DIM pos AS INTEGER

filename = "/tmp/grep_test_data.txt"
pattern = "pattern"
fileNum = 1
lineNum = 0
matchCount = 0

PRINT "Searching for '"; pattern; "' in "; filename
PRINT ""

OPEN filename FOR INPUT AS #fileNum

WHILE NOT EOF(#fileNum)
    LINE INPUT #fileNum, line
    lineNum = lineNum + 1

    ' Search for pattern in line
    pos = INSTR(line, pattern)

    ' Print if found
    IF pos > 0 THEN
        PRINT lineNum; ": "; line
        matchCount = matchCount + 1
    END IF
WEND

CLOSE #fileNum

PRINT ""
PRINT "Found "; matchCount; " matching lines"
END
