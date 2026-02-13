' glob_demo.bas
PRINT "=== Viper.IO.Glob Demo ==="
PRINT Viper.IO.Glob.Match("hello.txt", "*.txt")
PRINT Viper.IO.Glob.Match("hello.md", "*.txt")
PRINT "done"
END
