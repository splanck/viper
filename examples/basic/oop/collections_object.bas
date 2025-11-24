REM Example: Collections + Object methods

NAMESPACE App
  CLASS Person
  END CLASS
END NAMESPACE

DIM list AS Viper.System.Collections.List
list = NEW Viper.System.Collections.List()

DIM p1 AS App.Person
DIM p2 AS App.Person
p1 = NEW App.Person()
p2 = NEW App.Person()

list.Add(p1)
list.Add(p2)

PRINT list.Count
PRINT list.get_Item(0).ToString()
PRINT list.get_Item(1).Equals(p2)
PRINT list.get_Item(0).Equals(p2)

