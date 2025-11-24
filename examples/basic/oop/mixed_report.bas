REM Example: Mixed usage with List + String + (optional) File I/O

DIM list AS Viper.System.Collections.List
list = NEW Viper.System.Collections.List()

list.Add("alpha")
list.Add("beta")

DIM joined AS STRING
joined = list.get_Item(0).Concat("-").Concat(list.get_Item(1))
PRINT "report: "; joined

REM Optional file roundtrip (enable if your build exposes Viper.IO.File.*)
'Viper.IO.File.WriteAllText("oop_mixed_report.tmp", joined)
'PRINT Viper.IO.File.ReadAllText("oop_mixed_report.tmp")
'Viper.IO.File.Delete("oop_mixed_report.tmp")

