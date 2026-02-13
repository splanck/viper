' memstream_demo.bas
PRINT "=== Viper.IO.MemStream Demo ==="
DIM ms AS OBJECT
ms = NEW Viper.IO.MemStream()
PRINT ms.Pos
PRINT ms.Len
ms.WriteI8(42)
ms.WriteI16(1000)
ms.WriteStr("hello")
PRINT ms.Len
ms.Seek(0)
PRINT ms.ReadI8()
PRINT ms.ReadI16()
PRINT ms.ReadStr(5)
ms.Clear()
PRINT ms.Len
PRINT "done"
END
