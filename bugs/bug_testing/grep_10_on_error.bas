' Grep Clone Test 10: ON ERROR for error handling
DIM filename AS STRING
DIM fileNum AS INTEGER

filename = "/tmp/nonexistent_file.txt"
fileNum = 1

PRINT "Testing ON ERROR handling"

ON ERROR GOTO ErrorHandler

PRINT "Attempting to open: "; filename
OPEN filename FOR INPUT AS #fileNum
PRINT "File opened successfully"
CLOSE #fileNum

PRINT "Normal exit"
END

ErrorHandler:
    PRINT "Error occurred! File probably doesn't exist."
    END
