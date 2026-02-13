' jsonstream_demo.bas
PRINT "=== Viper.Text.JsonStream Demo ==="
DIM js AS OBJECT
js = NEW Viper.Text.JsonStream("{""a"":1,""b"":true}")
PRINT js.Depth
PRINT "done"
END
