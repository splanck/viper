' Test IS and AS operators with class inheritance
CLASS Parent
    PUBLIC value AS INTEGER
    PUBLIC VIRTUAL SUB Speak()
        PRINT "Parent speaks"
    END SUB
END CLASS

CLASS Child : Parent
    PUBLIC name AS STRING
    PUBLIC OVERRIDE SUB Speak()
        PRINT "Child speaks"
    END SUB
END CLASS

' Create a Child object, assign to Parent variable
DIM a AS Parent
a = NEW Child()

' Test IS operator with exact type
IF a IS Child THEN
    PRINT "is child"
END IF

' Test IS operator with base type (inheritance)
IF a IS Parent THEN
    PRINT "is parent"
END IF

' Test AS operator for safe cast
DIM c AS Child = a AS Child
IF c IS Child THEN
    PRINT "cast worked"
    c.value = 42
    PRINT c.value
END IF
