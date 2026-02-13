' trie_demo.bas
PRINT "=== Viper.Collections.Trie Demo ==="
DIM t AS OBJECT
t = NEW Viper.Collections.Trie()
t.Put("hello", "world")
t.Put("help", "me")
t.Put("world", "earth")
PRINT t.Len
PRINT t.Has("hello")
PRINT t.Has("hel")
PRINT t.HasPrefix("hel")
t.Remove("help")
PRINT t.Len
t.Clear()
PRINT t.IsEmpty
PRINT "done"
END
