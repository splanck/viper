CLASS TestClass
    FUNCTION GetHello() AS STRING
        DIM s AS STRING
        s = "Hello"
        GetHello = s
    END FUNCTION
END CLASS

DIM obj AS TestClass
obj = NEW TestClass()
PRINT obj.GetHello()
