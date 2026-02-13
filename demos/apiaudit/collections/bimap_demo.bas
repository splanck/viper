' bimap_demo.bas
PRINT "=== Viper.Collections.BiMap Demo ==="
DIM b AS OBJECT
b = NEW Viper.Collections.BiMap()
b.Put("en", "English")
b.Put("fr", "French")
b.Put("de", "German")
PRINT b.Len
PRINT b.GetByKey("en")
PRINT b.GetByValue("French")
PRINT b.HasKey("fr")
PRINT b.HasValue("Spanish")
b.RemoveByKey("de")
PRINT b.Len
b.Clear()
PRINT b.IsEmpty
PRINT "done"
END
