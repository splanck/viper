REM Test canonical Viper.* OOP runtime classes

REM 1. Test Viper.Object via user class
NAMESPACE Test
  CLASS MyClass
    SUB NEW()
    END SUB
  END CLASS
END NAMESPACE

DIM obj AS Test.MyClass
obj = NEW Test.MyClass()
PRINT "Object.ToString: "; obj.ToString()
REM GetHashCode returns non-zero value (varies per run, so just test it works)
IF obj.GetHashCode() <> 0 THEN PRINT "Object.GetHashCode: ok"
IF obj.Equals(obj) THEN PRINT "Object.Equals(self): true"

REM 2. Test Viper.String
DIM s AS STRING
s = "Hello World"
PRINT "String.Length: "; s.Length
PRINT "String.IsEmpty: "; s.IsEmpty
PRINT "String.Substring(0,5): "; s.Substring(0, 5)

REM 3. Test Viper.Text.StringBuilder
DIM sb AS Viper.Text.StringBuilder
sb = NEW Viper.Text.StringBuilder()
sb.Append("Hello ")
sb.Append("World")
PRINT "StringBuilder.ToString: "; sb.ToString()
PRINT "StringBuilder.Length: "; sb.Length

REM 4. Test Viper.Collections.List
DIM list AS Viper.Collections.List
list = NEW Viper.Collections.List()
list.Add(obj)
list.Add(obj)
PRINT "List.Count: "; list.Count
PRINT "List.get_Item(0).ToString: "; list.get_Item(0).ToString()
list.RemoveAt(0)
PRINT "List.Count after remove: "; list.Count
list.Clear()
PRINT "List.Count after clear: "; list.Count

PRINT "All canonical OOP runtime tests passed!"
