' Grep Clone Test 03: File reading with OPEN and LINE INPUT
DIM filename AS STRING
DIM line AS STRING
DIM fileNum AS INTEGER

filename = "/tmp/grep_test_data.txt"
fileNum = 1

OPEN filename FOR INPUT AS #fileNum

' Read and print all lines
DIM lineCount AS INTEGER
lineCount = 0

WHILE NOT EOF(#fileNum)
    LINE INPUT #fileNum, line
    lineCount = lineCount + 1
    PRINT "Line "; lineCount; ": "; line
WEND

CLOSE #fileNum

PRINT ""
PRINT "Total lines read: "; lineCount
END
