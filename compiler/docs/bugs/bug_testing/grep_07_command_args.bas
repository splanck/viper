' Grep Clone Test 07: Command line arguments
DIM args AS STRING
DIM argsLen AS INTEGER

args = ARGS
argsLen = LEN(args)

PRINT "Command line arguments: '"; args; "'"
PRINT "Length: "; argsLen

' Try to parse arguments (space-separated)
IF argsLen > 0 THEN
    PRINT "Arguments were provided"
ELSE
    PRINT "No arguments provided"
END IF

END
