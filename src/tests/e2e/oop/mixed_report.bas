REM Mixed usage: String + List + File + StringBuilder (procedural helpers to avoid keyword collisions)

DIM s AS STRING
DIM sb AS Viper.Text.StringBuilder
sb = NEW Viper.Text.StringBuilder()

REM Seed base text
s = "alpha beta"

REM Split into two parts using Substring (positions are deterministic)
DIM s1 AS STRING
DIM s2 AS STRING
s1 = s.Substring(0, 5)
s2 = s.Substring(6, 4)

DIM list AS Viper.Collections.List
list = NEW Viper.Collections.List()
list.Push(s1)
list.Push(s2)

PRINT list.Len
PRINT list.Get(0)
PRINT list.Get(1)

REM Build a simple report string using Concat and StringBuilder
DIM joined AS STRING
joined = list.Get(0).Concat("-").Concat(list.Get(1))

sb = Viper.Text.StringBuilder.Append(sb, "report:")
sb = Viper.Text.StringBuilder.Append(sb, " ")
sb = Viper.Text.StringBuilder.Append(sb, joined)

PRINT sb.ToString()

Viper.IO.File.WriteAllText("oop_mixed_report.tmp", sb.ToString())
PRINT Viper.IO.File.Exists("oop_mixed_report.tmp")
PRINT Viper.IO.File.ReadAllText("oop_mixed_report.tmp")
PRINT Viper.IO.File.Exists("oop_mixed_report.tmp")
