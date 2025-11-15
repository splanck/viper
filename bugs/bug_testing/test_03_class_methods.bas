' Test 03: Class with methods
CLASS GameObject
    DIM name AS STRING
    DIM description AS STRING

    SUB Init(n AS STRING, d AS STRING)
        name = n
        description = d
    END SUB

    FUNCTION GetInfo() AS STRING
        RETURN name + ": " + description
    END FUNCTION
END CLASS

DIM obj AS GameObject
obj = NEW GameObject()
obj.Init("Magic Sword", "A gleaming blade")
PRINT obj.GetInfo()
END
