' Edge case testing for DateTime operations

DIM ts AS INTEGER
DIM result AS STRING

' === Epoch and basic dates ===
PRINT "=== DateTime Basic ==="

ts = Viper.DateTime.Now()
PRINT "Now(): "; ts
PRINT "Year: "; Viper.DateTime.Year(ts)
PRINT "Month: "; Viper.DateTime.Month(ts)
PRINT "Day: "; Viper.DateTime.Day(ts)
PRINT "ToISO: "; Viper.DateTime.ToISO(ts)
PRINT ""

' Unix epoch
ts = 0
PRINT "=== Unix Epoch (0) ==="
PRINT "Year(0): "; Viper.DateTime.Year(0)
PRINT "Month(0): "; Viper.DateTime.Month(0)
PRINT "Day(0): "; Viper.DateTime.Day(0)
PRINT "Hour(0): "; Viper.DateTime.Hour(0)
PRINT "ToISO(0): "; Viper.DateTime.ToISO(0)
PRINT ""

' Negative timestamp (before epoch)
PRINT "=== Before Epoch (-86400) ==="
ts = -86400
PRINT "Year(-86400): "; Viper.DateTime.Year(ts)
PRINT "Month(-86400): "; Viper.DateTime.Month(ts)
PRINT "Day(-86400): "; Viper.DateTime.Day(ts)
PRINT "ToISO(-86400): "; Viper.DateTime.ToISO(ts)
PRINT ""

' Large timestamp (far future)
PRINT "=== Far Future ==="
ts = 4102444800  ' 2100-01-01
PRINT "Year(4102444800): "; Viper.DateTime.Year(ts)
PRINT "ToISO(4102444800): "; Viper.DateTime.ToISO(ts)
PRINT ""

' Very large timestamp
PRINT "=== Very Large Timestamp ==="
ts = 253402300799  ' 9999-12-31 23:59:59
PRINT "Year(253402300799): "; Viper.DateTime.Year(ts)
PRINT "ToISO: "; Viper.DateTime.ToISO(ts)
PRINT ""

' === Date arithmetic ===
PRINT "=== Date Arithmetic ==="
ts = 1704067200  ' 2024-01-01 00:00:00
PRINT "Base: "; Viper.DateTime.ToISO(ts)

DIM newTs AS INTEGER
newTs = Viper.DateTime.AddDays(ts, 1)
PRINT "AddDays(1): "; Viper.DateTime.ToISO(newTs)

newTs = Viper.DateTime.AddDays(ts, -1)
PRINT "AddDays(-1): "; Viper.DateTime.ToISO(newTs)

newTs = Viper.DateTime.AddDays(ts, 365)
PRINT "AddDays(365): "; Viper.DateTime.ToISO(newTs)

newTs = Viper.DateTime.AddSeconds(ts, 3600)
PRINT "AddSeconds(3600): "; Viper.DateTime.ToISO(newTs)

newTs = Viper.DateTime.AddSeconds(ts, -3600)
PRINT "AddSeconds(-3600): "; Viper.DateTime.ToISO(newTs)
PRINT ""

' === Create date ===
PRINT "=== Create Date ==="
ts = Viper.DateTime.Create(2024, 6, 15, 12, 30, 45)
PRINT "Create(2024,6,15,12,30,45): "; Viper.DateTime.ToISO(ts)

' Invalid date creation
ts = Viper.DateTime.Create(2024, 13, 1, 0, 0, 0)
PRINT "Create(2024,13,1,...) [invalid month]: "; Viper.DateTime.ToISO(ts)

ts = Viper.DateTime.Create(2024, 2, 30, 0, 0, 0)
PRINT "Create(2024,2,30,...) [invalid day]: "; Viper.DateTime.ToISO(ts)

ts = Viper.DateTime.Create(2024, 1, 1, 25, 0, 0)
PRINT "Create(2024,1,1,25,...) [invalid hour]: "; Viper.DateTime.ToISO(ts)
PRINT ""

' === Diff ===
PRINT "=== Date Diff ==="
DIM ts1 AS INTEGER
DIM ts2 AS INTEGER
ts1 = Viper.DateTime.Create(2024, 1, 1, 0, 0, 0)
ts2 = Viper.DateTime.Create(2024, 1, 2, 0, 0, 0)
PRINT "Diff(Jan 1, Jan 2): "; Viper.DateTime.Diff(ts1, ts2); " seconds"

ts2 = Viper.DateTime.Create(2023, 12, 31, 0, 0, 0)
PRINT "Diff(Jan 1 2024, Dec 31 2023): "; Viper.DateTime.Diff(ts1, ts2); " seconds"

PRINT ""
PRINT "=== DateTime Edge Case Tests Complete ==="
END
