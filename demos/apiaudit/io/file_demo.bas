' file_demo.bas
PRINT "=== Viper.IO.File Demo ==="
DIM tmp AS STRING
tmp = Viper.IO.TempFile.Path()
Viper.IO.File.WriteAllText(tmp, "hello world")
PRINT Viper.IO.File.Exists(tmp)
PRINT Viper.IO.File.ReadAllText(tmp)
PRINT Viper.IO.File.Size(tmp)
Viper.IO.File.Delete(tmp)
PRINT Viper.IO.File.Exists(tmp)
PRINT "done"
END
