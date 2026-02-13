' archive_demo.bas
PRINT "=== Viper.IO.Archive Demo ==="
DIM arc AS OBJECT
arc = Viper.IO.Archive.Create("/tmp/viper_test.zip")
arc.AddStr("hello.txt", "Hello World")
arc.Finish()
DIM arc2 AS OBJECT
arc2 = Viper.IO.Archive.Open("/tmp/viper_test.zip")
PRINT arc2.Count
PRINT arc2.Has("hello.txt")
PRINT arc2.ReadStr("hello.txt")
PRINT "done"
END
