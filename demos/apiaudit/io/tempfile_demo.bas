' tempfile_demo.bas
PRINT "=== Viper.IO.TempFile Demo ==="
PRINT Viper.IO.TempFile.Dir()
DIM p AS STRING
p = Viper.IO.TempFile.Path()
PRINT (LEN(p) > 0)
PRINT "done"
END
