' linereader_demo.bas
PRINT "=== Viper.IO.LineReader Demo ==="
DIM lw AS OBJECT
lw = NEW Viper.IO.LineWriter("/tmp/viper_lr_test.txt")
lw.WriteLn("hello")
lw.WriteLn("world")
lw.Close()
DIM lr AS OBJECT
lr = NEW Viper.IO.LineReader("/tmp/viper_lr_test.txt")
PRINT lr.Eof
PRINT lr.Read()
PRINT lr.Read()
lr.Close()
PRINT "done"
END
