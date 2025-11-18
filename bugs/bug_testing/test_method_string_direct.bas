CLASS TestClass
    FUNCTION GetHello() AS STRING
        GetHello = "Hello"
    END FUNCTION
END CLASS

DIM obj AS TestClass
obj = NEW TestClass()
PRINT obj.GetHello()
