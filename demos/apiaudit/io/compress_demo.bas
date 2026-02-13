' compress_demo.bas
PRINT "=== Viper.IO.Compress Demo ==="
DIM compressed AS OBJECT
compressed = Viper.IO.Compress.DeflateStr("hello world")
PRINT Viper.IO.Compress.InflateStr(compressed)
DIM gzipped AS OBJECT
gzipped = Viper.IO.Compress.GzipStr("test data")
PRINT Viper.IO.Compress.GunzipStr(gzipped)
PRINT "done"
END
