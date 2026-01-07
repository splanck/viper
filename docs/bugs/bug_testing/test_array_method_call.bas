REM Test: Calling methods on array elements

CLASS Item
    value AS INTEGER

    SUB SetValue(v AS INTEGER)
        ME.value = v
    END SUB

    FUNCTION GetValue() AS INTEGER
        GetValue = ME.value
    END FUNCTION
END CLASS

REM Test with array
DIM items(3) AS Item
DIM i AS INTEGER

FOR i = 1 TO 3
    items(i) = NEW Item()
    items(i).SetValue(i * 10)
NEXT i

PRINT "Values:"
FOR i = 1 TO 3
    PRINT "Item "; i; ": "; items(i).GetValue()
NEXT i
