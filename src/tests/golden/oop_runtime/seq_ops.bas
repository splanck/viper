DIM a AS Zanna.Collections.Seq
DIM b AS Zanna.Collections.Seq

a = NEW Zanna.Collections.Seq()
b = NEW Zanna.Collections.Seq()

a.Push("1")
a.Push("2")
b.Push("3")

a.PushAll(b)
PRINT Zanna.String.Join("|", a)

DIM self AS Zanna.Collections.Seq
self = NEW Zanna.Collections.Seq()
self.Push("a")
self.Push("b")
self.PushAll(self)
PRINT Zanna.String.Join("|", self)

DIM sh AS Zanna.Collections.Seq
sh = NEW Zanna.Collections.Seq()
sh.Push("a")
sh.Push("b")
sh.Push("c")
sh.Push("d")
sh.Push("e")

Zanna.Math.Random.Seed(1)
sh.Shuffle()
PRINT Zanna.String.Join("|", sh)

END

