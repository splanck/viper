' path_demo.bas
PRINT "=== Viper.IO.Path Demo ==="
PRINT Viper.IO.Path.Dir("/foo/bar/baz.txt")
PRINT Viper.IO.Path.Name("/foo/bar/baz.txt")
PRINT Viper.IO.Path.Ext("/foo/bar/baz.txt")
PRINT Viper.IO.Path.Stem("/foo/bar/baz.txt")
PRINT Viper.IO.Path.IsAbs("/foo/bar")
PRINT Viper.IO.Path.IsAbs("foo/bar")
PRINT Viper.IO.Path.Join("/foo", "bar")
PRINT Viper.IO.Path.WithExt("/foo/bar.txt", ".md")
PRINT Viper.IO.Path.Sep()
PRINT "done"
END
