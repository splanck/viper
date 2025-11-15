' Test 02: Class with fields
CLASS GameObject
    DIM name AS STRING
    DIM description AS STRING
END CLASS

DIM obj AS GameObject
obj = NEW GameObject()
obj.name = "Test Object"
obj.description = "A simple test object"
PRINT "Name: "; obj.name
PRINT "Description: "; obj.description
END
