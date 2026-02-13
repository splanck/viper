' =============================================================================
' API Audit: Viper.Time.Duration - Duration Handling
' =============================================================================
' Tests: FromMillis, FromSeconds, FromMinutes, FromHours, FromDays, Create, Zero,
'        TotalMillis, TotalSeconds, TotalMinutes, TotalHours, TotalDays,
'        TotalSecondsF, Days, Hours, Minutes, Seconds, Millis,
'        Add, Sub, Mul, Div, Abs, Neg, Cmp, ToString, ToISO
' =============================================================================

PRINT "=== API Audit: Viper.Time.Duration ==="

' --- FromMillis ---
PRINT "--- FromMillis ---"
DIM ms AS INTEGER
ms = Viper.Time.Duration.FromMillis(5000)
PRINT "FromMillis(5000): "; ms

' --- FromSeconds ---
PRINT "--- FromSeconds ---"
DIM sec AS INTEGER
sec = Viper.Time.Duration.FromSeconds(90)
PRINT "FromSeconds(90): "; sec

' --- FromMinutes ---
PRINT "--- FromMinutes ---"
DIM mn AS INTEGER
mn = Viper.Time.Duration.FromMinutes(5)
PRINT "FromMinutes(5): "; mn

' --- FromHours ---
PRINT "--- FromHours ---"
DIM hr AS INTEGER
hr = Viper.Time.Duration.FromHours(2)
PRINT "FromHours(2): "; hr

' --- FromDays ---
PRINT "--- FromDays ---"
DIM dy AS INTEGER
dy = Viper.Time.Duration.FromDays(1)
PRINT "FromDays(1): "; dy

' --- Create ---
PRINT "--- Create ---"
DIM cx AS INTEGER
cx = Viper.Time.Duration.Create(1, 2, 30, 15, 500)
PRINT "Create(1,2,30,15,500): "; cx

' --- Zero ---
PRINT "--- Zero ---"
PRINT "Zero: "; Viper.Time.Duration.Zero()

' --- Totals ---
PRINT "--- Totals ---"
PRINT "TotalMillis(90s): "; Viper.Time.Duration.TotalMillis(sec)
PRINT "TotalSeconds(5m): "; Viper.Time.Duration.TotalSeconds(mn)
PRINT "TotalMinutes(2h): "; Viper.Time.Duration.TotalMinutes(hr)
PRINT "TotalHours(1d): "; Viper.Time.Duration.TotalHours(dy)
PRINT "TotalDays(1d): "; Viper.Time.Duration.TotalDays(dy)
PRINT "TotalSecondsF(5500ms): "; Viper.Time.Duration.TotalSecondsF(Viper.Time.Duration.FromMillis(5500))

' --- Components ---
PRINT "--- Components ---"
PRINT "Days: "; Viper.Time.Duration.Days(cx)
PRINT "Hours: "; Viper.Time.Duration.Hours(cx)
PRINT "Minutes: "; Viper.Time.Duration.Minutes(cx)
PRINT "Seconds: "; Viper.Time.Duration.Seconds(cx)
PRINT "Millis: "; Viper.Time.Duration.Millis(cx)

' --- Arithmetic ---
PRINT "--- Arithmetic ---"
PRINT "Add(90s,5m) TotalSec: "; Viper.Time.Duration.TotalSeconds(Viper.Time.Duration.Add(sec, mn))
PRINT "Sub(5m,90s) TotalSec: "; Viper.Time.Duration.TotalSeconds(Viper.Time.Duration.Sub(mn, sec))
PRINT "Mul(90s,2) TotalSec: "; Viper.Time.Duration.TotalSeconds(Viper.Time.Duration.Mul(sec, 2))
PRINT "Div(90s,2) TotalSec: "; Viper.Time.Duration.TotalSeconds(Viper.Time.Duration.Div(sec, 2))

' --- Abs / Neg ---
PRINT "--- Abs / Neg ---"
DIM negD AS INTEGER
negD = Viper.Time.Duration.Neg(sec)
PRINT "Neg(90s) TotalMillis: "; Viper.Time.Duration.TotalMillis(negD)
PRINT "Abs(neg) TotalSec: "; Viper.Time.Duration.TotalSeconds(Viper.Time.Duration.Abs(negD))

' --- Cmp ---
PRINT "--- Cmp ---"
PRINT "Cmp(90s,5m): "; Viper.Time.Duration.Cmp(sec, mn)
PRINT "Cmp(5m,90s): "; Viper.Time.Duration.Cmp(mn, sec)
PRINT "Cmp(90s,90s): "; Viper.Time.Duration.Cmp(sec, sec)

' --- ToString / ToISO ---
PRINT "--- ToString / ToISO ---"
PRINT "ToString: "; Viper.Time.Duration.ToString(cx)
PRINT "ToISO: "; Viper.Time.Duration.ToISO(cx)

PRINT "=== Duration Demo Complete ==="
END
