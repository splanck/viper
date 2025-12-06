REM Test standalone NEW

CLASS Simple
    value AS INTEGER
END CLASS

PRINT "Creating with NEW..."
DIM obj AS Simple
obj = NEW Simple()
PRINT "Created!"
obj.value = 42
PRINT "Value: "; obj.value
