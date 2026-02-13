' =============================================================================
' API Audit: Viper.Text.CompiledPattern (BASIC)
' =============================================================================
' Tests: New, Pattern, IsMatch, Find, FindFrom, FindPos, FindAll,
'        Captures, CapturesFrom, Replace, ReplaceFirst, Split, SplitN
' =============================================================================

PRINT "=== API Audit: Viper.Text.CompiledPattern ==="

' --- New ---
PRINT "--- New ---"
DIM pat AS OBJECT = Viper.Text.CompiledPattern.New("[0-9]+")
PRINT "Created CompiledPattern for '[0-9]+'"

' --- Pattern ---
PRINT "--- Pattern ---"
PRINT pat.Pattern

' --- IsMatch ---
PRINT "--- IsMatch ---"
PRINT "IsMatch 'abc123': "; pat.IsMatch("abc123")
PRINT "IsMatch 'hello': "; pat.IsMatch("hello")

' --- Find ---
PRINT "--- Find ---"
PRINT "Find 'abc123def': "; pat.Find("abc123def")

' --- FindFrom ---
PRINT "--- FindFrom ---"
PRINT "FindFrom pos 6: "; pat.FindFrom("abc123def456", 6)

' --- FindPos ---
PRINT "--- FindPos ---"
PRINT "FindPos 'abc123def': "; pat.FindPos("abc123def")
PRINT "FindPos 'abcdef': "; pat.FindPos("abcdef")

' --- FindAll ---
PRINT "--- FindAll ---"
DIM matches AS OBJECT = pat.FindAll("a1b22c333")
PRINT "Match count: "; matches.Len
DIM m0 AS OBJECT = matches.Get(0)
PRINT "Match 0: "; m0
DIM m1 AS OBJECT = matches.Get(1)
PRINT "Match 1: "; m1
DIM m2 AS OBJECT = matches.Get(2)
PRINT "Match 2: "; m2

' --- Captures ---
PRINT "--- Captures ---"
DIM capPat AS OBJECT = Viper.Text.CompiledPattern.New("(\w+)@(\w+)")
DIM caps AS OBJECT = capPat.Captures("user@host")
PRINT "Capture count: "; caps.Len
DIM c0 AS OBJECT = caps.Get(0)
PRINT "Capture 0: "; c0
DIM c1 AS OBJECT = caps.Get(1)
PRINT "Capture 1: "; c1
DIM c2 AS OBJECT = caps.Get(2)
PRINT "Capture 2: "; c2

' --- CapturesFrom ---
PRINT "--- CapturesFrom ---"
DIM caps2 AS OBJECT = pat.CapturesFrom("abc123def456", 6)
PRINT "Capture count: "; caps2.Len
DIM cf0 AS OBJECT = caps2.Get(0)
PRINT "Capture 0: "; cf0

' --- Replace ---
PRINT "--- Replace ---"
PRINT pat.Replace("abc123def456", "NUM")

' --- ReplaceFirst ---
PRINT "--- ReplaceFirst ---"
PRINT pat.ReplaceFirst("abc123def456", "NUM")

' --- Split ---
PRINT "--- Split ---"
DIM commaPat AS OBJECT = Viper.Text.CompiledPattern.New("[,;]+")
DIM parts AS OBJECT = commaPat.Split("one,two;;three,four")
PRINT "Part count: "; parts.Len
DIM p0 AS OBJECT = parts.Get(0)
PRINT "Part 0: "; p0
DIM p1 AS OBJECT = parts.Get(1)
PRINT "Part 1: "; p1
DIM p2 AS OBJECT = parts.Get(2)
PRINT "Part 2: "; p2
DIM p3 AS OBJECT = parts.Get(3)
PRINT "Part 3: "; p3

' --- SplitN ---
PRINT "--- SplitN ---"
DIM parts2 AS OBJECT = commaPat.SplitN("one,two;;three,four", 2)
PRINT "Part count: "; parts2.Len
DIM sn0 AS OBJECT = parts2.Get(0)
PRINT "Part 0: "; sn0
DIM sn1 AS OBJECT = parts2.Get(1)
PRINT "Part 1: "; sn1

' --- Second pattern ---
PRINT "--- Second pattern ---"
DIM wordPat AS OBJECT = Viper.Text.CompiledPattern.New("[A-Z][a-z]+")
PRINT wordPat.Pattern
PRINT "Find: "; wordPat.Find("hello World foo Bar")
PRINT "IsMatch 'hello': "; wordPat.IsMatch("hello")
PRINT "IsMatch 'Hello': "; wordPat.IsMatch("Hello")

PRINT "=== CompiledPattern Audit Complete ==="
END
