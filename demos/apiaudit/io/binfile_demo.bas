' binfile_demo.bas
PRINT "=== Viper.IO.BinFile Demo ==="
DIM bf AS OBJECT
bf = NEW Viper.IO.BinFile("/tmp/viper_binfile_test.bin", "w")
bf.WriteByte(65)
bf.WriteByte(66)
bf.Flush()
bf.Close()
DIM bf2 AS OBJECT
bf2 = NEW Viper.IO.BinFile("/tmp/viper_binfile_test.bin", "r")
PRINT bf2.Size
PRINT bf2.ReadByte()
PRINT bf2.ReadByte()
PRINT bf2.Eof
bf2.Close()
PRINT "done"
END
