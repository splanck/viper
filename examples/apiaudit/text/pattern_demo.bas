' Viper.Text.Pattern API Audit - Regex Pattern Matching
' Tests all Pattern functions
' Note: Pattern functions take (text, pattern) argument order

PRINT "=== Viper.Text.Pattern API Audit ==="

' --- IsMatch ---
PRINT "--- IsMatch ---"
PRINT "IsMatch digits in 'abc123': "; Viper.Text.Pattern.IsMatch("abc123", "[0-9]+")
PRINT "IsMatch digits in 'hello': "; Viper.Text.Pattern.IsMatch("hello", "[0-9]+")
PRINT "IsMatch lowercase 'hello': "; Viper.Text.Pattern.IsMatch("hello", "^[a-z]+$")
PRINT "IsMatch lowercase 'Hello': "; Viper.Text.Pattern.IsMatch("Hello", "^[a-z]+$")

' --- Find ---
PRINT "--- Find ---"
PRINT "Find digits: "; Viper.Text.Pattern.Find("abc123def", "[0-9]+")
PRINT "Find word: "; Viper.Text.Pattern.Find("Hello World", "[A-Z][a-z]+")

' --- FindFrom ---
PRINT "--- FindFrom ---"
PRINT "FindFrom pos 6: "; Viper.Text.Pattern.FindFrom("abc123def456", "[0-9]+", 6)

' --- FindPos ---
PRINT "--- FindPos ---"
PRINT "FindPos digits: "; Viper.Text.Pattern.FindPos("abc123def", "[0-9]+")
PRINT "FindPos no match: "; Viper.Text.Pattern.FindPos("abcdef", "[0-9]+")

' --- FindAll ---
PRINT "--- FindAll ---"
DIM matches AS Viper.Collections.Seq
matches = Viper.Text.Pattern.FindAll("a1b22c333", "[0-9]+")
PRINT "Match count: "; matches.Len
IF matches.Len > 0 THEN
    DIM m0 AS OBJECT
    m0 = matches.Get(0)
    PRINT "Match 0: "; m0
END IF
IF matches.Len > 1 THEN
    DIM m1 AS OBJECT
    m1 = matches.Get(1)
    PRINT "Match 1: "; m1
END IF
IF matches.Len > 2 THEN
    DIM m2 AS OBJECT
    m2 = matches.Get(2)
    PRINT "Match 2: "; m2
END IF

' --- Replace ---
PRINT "--- Replace ---"
PRINT Viper.Text.Pattern.Replace("abc123def456", "[0-9]+", "NUM")
PRINT Viper.Text.Pattern.Replace("hello   world   foo", "\s+", " ")

' --- ReplaceFirst ---
PRINT "--- ReplaceFirst ---"
PRINT Viper.Text.Pattern.ReplaceFirst("abc123def456", "[0-9]+", "NUM")

' --- Split ---
PRINT "--- Split ---"
DIM parts AS Viper.Collections.Seq
parts = Viper.Text.Pattern.Split("one,two;;three,four", "[,;]+")
PRINT "Part count: "; parts.Len
IF parts.Len > 0 THEN
    DIM p0 AS OBJECT
    p0 = parts.Get(0)
    PRINT "Part 0: "; p0
END IF
IF parts.Len > 1 THEN
    DIM p1 AS OBJECT
    p1 = parts.Get(1)
    PRINT "Part 1: "; p1
END IF
IF parts.Len > 2 THEN
    DIM p2 AS OBJECT
    p2 = parts.Get(2)
    PRINT "Part 2: "; p2
END IF
IF parts.Len > 3 THEN
    DIM p3 AS OBJECT
    p3 = parts.Get(3)
    PRINT "Part 3: "; p3
END IF

' --- Escape ---
PRINT "--- Escape ---"
PRINT Viper.Text.Pattern.Escape("hello.world+foo")
PRINT Viper.Text.Pattern.Escape("[test](value)")

PRINT "=== Pattern Demo Complete ==="
END
