REM Collections + Object demo: List + Object methods (no fields to avoid property surface)

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

PRINT list.Count              REM 2
PRINT list.get_Item(0).ToString()   REM "App.Person"
PRINT list.get_Item(1).Equals(p2)   REM 1
PRINT list.get_Item(0).Equals(p2)   REM 0

list.RemoveAt(0)
PRINT list.Count              REM 1

list.set_Item(0, p1)
PRINT list.get_Item(0).Equals(p1)   REM 1

list.Clear()
PRINT list.Count              REM 0
