' Viper.Text.Csv API Audit - CSV Parsing and Formatting
' Tests all Csv functions
' Note: Inline row.Get(N) in PRINT can cause heap corruption.
'       Store Get results in variables first.

PRINT "=== Viper.Text.Csv API Audit ==="

' --- ParseLine ---
PRINT "--- ParseLine ---"
DIM row AS Viper.Collections.Seq
row = Viper.Text.Csv.ParseLine("Alice,30,Boston")
PRINT "Field count: "; row.Len
DIM rf0 AS OBJECT
rf0 = row.Get(0)
PRINT "Field 0: "; rf0
DIM rf1 AS OBJECT
rf1 = row.Get(1)
PRINT "Field 1: "; rf1
DIM rf2 AS OBJECT
rf2 = row.Get(2)
PRINT "Field 2: "; rf2

' Quoted fields
DIM row2 AS Viper.Collections.Seq
row2 = Viper.Text.Csv.ParseLine(CHR(34) + "hello, world" + CHR(34) + ",42," + CHR(34) + "quoted" + CHR(34))
PRINT "Quoted field count: "; row2.Len
DIM qf0 AS OBJECT
qf0 = row2.Get(0)
PRINT "Quoted field 0: "; qf0
DIM qf1 AS OBJECT
qf1 = row2.Get(1)
PRINT "Quoted field 1: "; qf1

' --- ParseLineWith (custom delimiter) ---
PRINT "--- ParseLineWith ---"
DIM row3 AS Viper.Collections.Seq
row3 = Viper.Text.Csv.ParseLineWith("one;two;three", ";")
PRINT "Field count: "; row3.Len
DIM df0 AS OBJECT
df0 = row3.Get(0)
PRINT "Field 0: "; df0
DIM df1 AS OBJECT
df1 = row3.Get(1)
PRINT "Field 1: "; df1
DIM df2 AS OBJECT
df2 = row3.Get(2)
PRINT "Field 2: "; df2

' --- Parse (multi-line CSV) ---
PRINT "--- Parse ---"
DIM csv AS STRING
csv = "name,age,city" + CHR(10) + "Alice,30,Boston" + CHR(10) + "Bob,25,NYC"
DIM rows AS Viper.Collections.Seq
rows = Viper.Text.Csv.Parse(csv)
PRINT "Row count: "; rows.Len

' --- ParseWith (multi-line, custom delimiter) ---
PRINT "--- ParseWith ---"
DIM csv2 AS STRING
csv2 = "a|b|c" + CHR(10) + "1|2|3" + CHR(10) + "4|5|6"
DIM rows2 AS Viper.Collections.Seq
rows2 = Viper.Text.Csv.ParseWith(csv2, "|")
PRINT "Row count: "; rows2.Len

' --- FormatLine ---
PRINT "--- FormatLine ---"
DIM fields AS OBJECT
fields = Viper.Collections.Seq.New()
fields.Push(Viper.Core.Box.Str("Alice"))
fields.Push(Viper.Core.Box.Str("30"))
fields.Push(Viper.Core.Box.Str("Boston"))
PRINT Viper.Text.Csv.FormatLine(fields)

' --- FormatLineWith ---
PRINT "--- FormatLineWith ---"
PRINT Viper.Text.Csv.FormatLineWith(fields, ";")

' --- Format (multi-row) ---
PRINT "--- Format ---"
DIM tbl AS OBJECT
tbl = Viper.Collections.Seq.New()
DIM r1 AS OBJECT
r1 = Viper.Collections.Seq.New()
r1.Push(Viper.Core.Box.Str("name"))
r1.Push(Viper.Core.Box.Str("age"))
tbl.Push(r1)
DIM r2 AS OBJECT
r2 = Viper.Collections.Seq.New()
r2.Push(Viper.Core.Box.Str("Alice"))
r2.Push(Viper.Core.Box.Str("30"))
tbl.Push(r2)
PRINT Viper.Text.Csv.Format(tbl)

' --- FormatWith ---
PRINT "--- FormatWith ---"
PRINT Viper.Text.Csv.FormatWith(tbl, "|")

PRINT "=== Csv Demo Complete ==="
END
