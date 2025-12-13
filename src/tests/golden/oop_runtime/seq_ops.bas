DIM a AS Viper.Collections.Seq
DIM b AS Viper.Collections.Seq

a = NEW Viper.Collections.Seq()
b = NEW Viper.Collections.Seq()

a.Push("1")
a.Push("2")
b.Push("3")

a.PushAll(b)
PRINT Viper.Strings.Join("|", a)

DIM self AS Viper.Collections.Seq
self = NEW Viper.Collections.Seq()
self.Push("a")
self.Push("b")
self.PushAll(self)
PRINT Viper.Strings.Join("|", self)

DIM sh AS Viper.Collections.Seq
sh = NEW Viper.Collections.Seq()
sh.Push("a")
sh.Push("b")
sh.Push("c")
sh.Push("d")
sh.Push("e")

Viper.Random.Seed(1)
sh.Shuffle()
PRINT Viper.Strings.Join("|", sh)

END

