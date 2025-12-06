' Test 09b: Single-line IF
CLASS Test
    DIM value AS INTEGER

    SUB CheckValue()
        IF value = 0 THEN PRINT "Value is zero"
    END SUB
END CLASS

DIM t AS Test
t = NEW Test()
t.value = 0
t.CheckValue()
t.value = 5
t.CheckValue()
END
