REM OOP e2e: List + String + Object (no StringBuilder/File to avoid keyword collisions)

CLASS Person
END CLASS

DIM list AS Viper.System.Collections.List
list = NEW Viper.System.Collections.List()

DIM p1 AS Person
DIM p2 AS Person
p1 = NEW Person()
p2 = NEW Person()

list.Add(p1)
list.Add(p2)

PRINT list.Count
PRINT list.get_Item(0).ToString()
PRINT list.get_Item(1).Equals(p2)

DIM s1 AS STRING
DIM s2 AS STRING
DIM joined AS STRING
s1 = "alpha"
s2 = "beta"
joined = s1.Concat("-").Concat(s2)
PRINT joined
