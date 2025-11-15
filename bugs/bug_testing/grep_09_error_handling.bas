' Grep Clone Test 09: Error handling for missing files
' Test what happens when file doesn't exist

DIM filename AS STRING
DIM fileNum AS INTEGER
DIM line AS STRING

filename = "/tmp/nonexistent_file.txt"
fileNum = 1

PRINT "Attempting to open: "; filename

' Try to open the file
OPEN filename FOR INPUT AS #fileNum

' If we get here, file opened successfully
PRINT "File opened successfully"

' Try to read
WHILE NOT EOF(#fileNum)
    LINE INPUT #fileNum, line
    PRINT line
WEND

CLOSE #fileNum

PRINT "Done"
END
