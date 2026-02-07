REM Collections + Object demo: List + Object methods (no fields to avoid property surface)

NAMESPACE App
  CLASS Person
  END CLASS
END NAMESPACE

DIM list AS Viper.Collections.List
list = NEW Viper.Collections.List()

DIM p1 AS App.Person
DIM p2 AS App.Person
p1 = NEW App.Person()
p2 = NEW App.Person()

list.Push(p1)
list.Push(p2)

PRINT list.Len              REM 2
PRINT list.Get(0).ToString()   REM "App.Person"
PRINT list.Get(1).Equals(p2)   REM 1
PRINT list.Get(0).Equals(p2)   REM 0

list.RemoveAt(0)
PRINT list.Len              REM 1

list.Set(0, p1)
PRINT list.Get(0).Equals(p1)   REM 1

list.Clear()
PRINT list.Len              REM 0
