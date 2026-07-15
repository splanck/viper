' test_io_path.bas — Viper.IO.Path + Viper.IO.File + Viper.IO.Dir
PRINT Viper.IO.Path.Join("a", "b")
PRINT Viper.IO.Path.Extension("file.txt")
PRINT Viper.IO.Path.Name("dir/file.txt")
PRINT Viper.IO.Path.Stem("dir/file.txt")
PRINT Viper.IO.Path.Directory("dir/file.txt")
PRINT (Viper.IO.Path.Extension("file.txt") <> "")
PRINT (Viper.IO.Path.Extension("file") <> "")
PRINT Viper.IO.Path.WithExtension("file.txt", ".md")
PRINT Viper.IO.Path.IsAbsolute("C:\foo")
PRINT Viper.IO.Path.IsAbsolute("foo")
PRINT Viper.IO.Path.Normalize("a/b/../c")

' File write/read round-trip
DIM tmp AS STRING
LET tmp = "_rt_api_test_tmp.txt"
Viper.IO.File.WriteAllText(tmp, "hello from viper")
PRINT Viper.IO.File.Exists(tmp)
PRINT Viper.IO.File.ReadAllText(tmp)
PRINT Viper.IO.File.SizeBytes(tmp)
Viper.IO.File.Append(tmp, "!")
PRINT Viper.IO.File.ReadAllText(tmp)
Viper.IO.File.Delete(tmp)
PRINT Viper.IO.File.Exists(tmp)

' Dir
PRINT Viper.IO.Dir.Exists(".")
PRINT "done"
END
