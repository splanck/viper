' EXPECT_OUT: RESULT: ok
' COVER: Viper.String.FromStr
' COVER: Viper.String.IsEmpty
' COVER: Viper.String.Length
' COVER: Viper.String.Asc
' COVER: Viper.String.Chr
' COVER: Viper.String.Cmp
' COVER: Viper.String.CmpNoCase
' COVER: Viper.String.Concat
' COVER: Viper.String.Count
' COVER: Viper.String.EndsWith
' COVER: Viper.String.Flip
' COVER: Viper.String.Has
' COVER: Viper.String.IndexOf
' COVER: Viper.String.IndexOfFrom
' COVER: Viper.String.Left
' COVER: Viper.String.Mid
' COVER: Viper.String.MidLen
' COVER: Viper.String.PadLeft
' COVER: Viper.String.PadRight
' COVER: Viper.String.Repeat
' COVER: Viper.String.Replace
' COVER: Viper.String.Right
' COVER: Viper.String.Split
' COVER: Viper.String.StartsWith
' COVER: Viper.String.Substring
' COVER: Viper.String.ToLower
' COVER: Viper.String.ToUpper
' COVER: Viper.String.Trim
' COVER: Viper.String.TrimEnd
' COVER: Viper.String.TrimStart
' COVER: Viper.Object.Equals
' COVER: Viper.Object.GetHashCode
' COVER: Viper.Object.ToString

SUB AssertApprox(actual AS DOUBLE, expected AS DOUBLE, eps AS DOUBLE, msg AS STRING)
    IF Viper.Math.Abs(actual - expected) > eps THEN
        Viper.Diagnostics.Assert(FALSE, msg)
    END IF
END SUB

DIM s AS STRING
s = "  AbCd  "
Viper.Diagnostics.Assert(("".IsEmpty), "str.empty")
Viper.Diagnostics.AssertEq(("abcd").Length, 4, "str.len")
Viper.Diagnostics.AssertEqStr(Viper.String.FromStr("x"), "x", "str.fromstr")

Viper.Diagnostics.AssertEqStr(Viper.String.Trim(s), "AbCd", "str.trim")
Viper.Diagnostics.AssertEqStr(Viper.String.TrimStart(s), "AbCd  ", "str.trimstart")
Viper.Diagnostics.AssertEqStr(Viper.String.TrimEnd(s), "  AbCd", "str.trimend")
Viper.Diagnostics.AssertEqStr(Viper.String.Left("abcd", 2), "ab", "str.left")
Viper.Diagnostics.AssertEqStr(Viper.String.Right("abcd", 2), "cd", "str.right")
Viper.Diagnostics.AssertEqStr(Viper.String.Mid("abcd", 2), "bcd", "str.mid")
Viper.Diagnostics.AssertEqStr(Viper.String.MidLen("abcd", 2, 2), "bc", "str.midlen")
Viper.Diagnostics.AssertEqStr(Viper.String.Substring("abcd", 1, 2), "bc", "str.substr")
Viper.Diagnostics.AssertEqStr(Viper.String.Concat("ab", "cd"), "abcd", "str.concat")
Viper.Diagnostics.AssertEqStr(Viper.String.ToUpper("aB"), "AB", "str.upper")
Viper.Diagnostics.AssertEqStr(Viper.String.ToLower("aB"), "ab", "str.lower")
Viper.Diagnostics.AssertEqStr(Viper.String.Replace("a-b-a", "-", "+"), "a+b+a", "str.replace")
Viper.Diagnostics.AssertEqStr(Viper.String.PadLeft("7", 3, "0"), "007", "str.padleft")
Viper.Diagnostics.AssertEqStr(Viper.String.PadRight("7", 3, "."), "7..", "str.padright")
Viper.Diagnostics.AssertEqStr(Viper.String.Repeat("ab", 3), "ababab", "str.repeat")
Viper.Diagnostics.AssertEqStr(Viper.String.Flip("abc"), "cba", "str.flip")
Viper.Diagnostics.AssertEqStr(Viper.String.Chr(66), "B", "str.chr")
Viper.Diagnostics.AssertEq(Viper.String.Asc("A"), 65, "str.asc")
Viper.Diagnostics.AssertEq(Viper.String.IndexOf("abcd", "cd"), 3, "str.indexof")
Viper.Diagnostics.AssertEq(Viper.String.IndexOfFrom("ababa", 2, "ba"), 2, "str.indexoffrom")
Viper.Diagnostics.AssertEq(Viper.String.Count("abab", "ab"), 2, "str.count")
Viper.Diagnostics.Assert(Viper.String.Has("hello", "ell"), "str.has")
Viper.Diagnostics.Assert(Viper.String.StartsWith("hello", "he"), "str.startswith")
Viper.Diagnostics.Assert(Viper.String.EndsWith("hello", "lo"), "str.endswith")
Viper.Diagnostics.AssertEq(Viper.String.Cmp("a", "b"), -1, "str.cmp")
Viper.Diagnostics.AssertEq(Viper.String.CmpNoCase("A", "a"), 0, "str.cmpnocase")

DIM parts AS Viper.Collections.Seq
parts = Viper.String.Split("a,b,c", ",")
Viper.Diagnostics.AssertEq(parts.Len, 3, "str.split.len")
Viper.Diagnostics.AssertEqStr(parts.Get(0), "a", "str.split.get")

DIM objA AS Viper.Collections.List
DIM objB AS Viper.Collections.List
objA = NEW Viper.Collections.List()
objB = NEW Viper.Collections.List()
Viper.Diagnostics.Assert(Viper.Object.Equals(objA, objA), "obj.equals.self")
Viper.Diagnostics.Assert(Viper.Object.Equals(objA, objB) = FALSE, "obj.equals.other")
Viper.Diagnostics.Assert(Viper.Object.ToString(objA) <> "", "obj.tostring")
Viper.Diagnostics.Assert(Viper.Object.GetHashCode(objA) <> 0, "obj.hash")

PRINT "RESULT: ok"
END
