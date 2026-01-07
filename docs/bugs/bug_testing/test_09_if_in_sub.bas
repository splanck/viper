' Test 09: IF/THEN inside SUB
CLASS Test
    DIM value AS INTEGER

    SUB CheckValue()
        IF value = 0 THEN
            PRINT "Value is zero"
        END IF
    END SUB

    SUB CheckValue2()
        IF value = 0 THEN
            PRINT "Zero"
        ELSE
            PRINT "Not zero"
        END IF
    END SUB
END CLASS

DIM t AS Test
t = NEW Test()
t.value = 0
t.CheckValue()
t.CheckValue2()
t.value = 5
t.CheckValue()
t.CheckValue2()
END
