' =============================================================================
' API Audit: Viper.Time.DateRange - Date Range Operations
' =============================================================================
' Tests: New, Start, End, Contains, Overlaps, Intersection, Union, Days,
'        Hours, Duration, ToString
' =============================================================================

PRINT "=== API Audit: Viper.Time.DateRange ==="

DIM start1 AS INTEGER
DIM end1 AS INTEGER
DIM start2 AS INTEGER
DIM end2 AS INTEGER
start1 = Viper.Time.DateTime.Create(2024, 1, 1, 0, 0, 0)
end1 = Viper.Time.DateTime.Create(2024, 1, 31, 23, 59, 59)
start2 = Viper.Time.DateTime.Create(2024, 1, 15, 0, 0, 0)
end2 = Viper.Time.DateTime.Create(2024, 2, 15, 23, 59, 59)

' --- New ---
PRINT "--- New ---"
DIM r1 AS OBJECT
DIM r2 AS OBJECT
r1 = Viper.Time.DateRange.New(start1, end1)
PRINT "Range 1: Jan 1 - Jan 31"
r2 = Viper.Time.DateRange.New(start2, end2)
PRINT "Range 2: Jan 15 - Feb 15"

' --- Start / End ---
PRINT "--- Start / End ---"
PRINT "r1 Start ISO: "; Viper.Time.DateTime.ToISO(Viper.Time.DateRange.get_Start(r1))
PRINT "r1 End ISO: "; Viper.Time.DateTime.ToISO(Viper.Time.DateRange.get_End(r1))

' --- Contains ---
PRINT "--- Contains ---"
DIM midJan AS INTEGER
midJan = Viper.Time.DateTime.Create(2024, 1, 15, 12, 0, 0)
DIM midFeb AS INTEGER
midFeb = Viper.Time.DateTime.Create(2024, 2, 15, 12, 0, 0)
PRINT "r1 Contains(Jan 15): "; Viper.Time.DateRange.Contains(r1, midJan)
PRINT "r1 Contains(Feb 15): "; Viper.Time.DateRange.Contains(r1, midFeb)

' --- Overlaps ---
PRINT "--- Overlaps ---"
PRINT "r1 Overlaps r2: "; Viper.Time.DateRange.Overlaps(r1, r2)

' --- Intersection ---
PRINT "--- Intersection ---"
DIM inter AS OBJECT
inter = Viper.Time.DateRange.Intersection(r1, r2)
PRINT "Intersection Start: "; Viper.Time.DateTime.ToISO(Viper.Time.DateRange.get_Start(inter))
PRINT "Intersection End: "; Viper.Time.DateTime.ToISO(Viper.Time.DateRange.get_End(inter))

' --- Union ---
PRINT "--- Union ---"
DIM uni AS OBJECT
uni = Viper.Time.DateRange.Union(r1, r2)
PRINT "Union Start: "; Viper.Time.DateTime.ToISO(Viper.Time.DateRange.get_Start(uni))
PRINT "Union End: "; Viper.Time.DateTime.ToISO(Viper.Time.DateRange.get_End(uni))

' --- Days ---
PRINT "--- Days ---"
PRINT "r1 Days: "; Viper.Time.DateRange.Days(r1)

' --- Hours ---
PRINT "--- Hours ---"
PRINT "r1 Hours: "; Viper.Time.DateRange.Hours(r1)

' --- Duration ---
PRINT "--- Duration ---"
PRINT "r1 Duration: "; Viper.Time.DateRange.Duration(r1)

' --- ToString ---
PRINT "--- ToString ---"
PRINT "r1 ToString: "; Viper.Time.DateRange.ToString(r1)

PRINT "=== DateRange Demo Complete ==="
END
