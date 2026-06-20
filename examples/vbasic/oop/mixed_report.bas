REM Example: Mixed usage with List + String + (optional) File I/O

DIM list AS Viper.Collections.List
list = NEW Viper.Collections.List()

list.Push("alpha")
list.Push("beta")

DIM joined AS STRING
joined = Viper.Core.Box.ToStr(list.Get(0)) + "-" + Viper.Core.Box.ToStr(list.Get(1))
PRINT "report: "; joined

REM Optional file roundtrip (enable if your build exposes Viper.IO.File.*)
'Viper.IO.File.WriteAllText("oop_mixed_report.tmp", joined)
'PRINT Viper.IO.File.ReadAllText("oop_mixed_report.tmp")
'Viper.IO.File.Delete("oop_mixed_report.tmp")
