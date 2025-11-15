' Test array access that might cause assertion
DIM arr1(8) AS INTEGER
DIM arr2(8) AS INTEGER
DIM i AS INTEGER

' Initialize
FOR i = 0 TO 7
    arr1(i) = i
    arr2(i) = i * 10
NEXT i

' Access in a function
FUNCTION TestAccess(index AS INTEGER) AS INTEGER
    DIM val AS INTEGER
    val = arr1(index)  ' Access global array in function
    RETURN val
END FUNCTION

DIM result AS INTEGER
result = TestAccess(3)
PRINT "Result: "; result

END
