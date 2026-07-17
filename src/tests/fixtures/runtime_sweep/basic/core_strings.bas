' EXPECT_OUT: RESULT: ok
' COVER: Zanna.String.IsEmpty
' COVER: Zanna.String.get_Length
' COVER: Zanna.String.Asc
' COVER: Zanna.String.Chr
' COVER: Zanna.String.Compare
' COVER: Zanna.String.CompareIgnoreCase
' COVER: Zanna.String.Concat
' COVER: Zanna.String.Count
' COVER: Zanna.String.EndsWith
' COVER: Zanna.String.Reverse
' COVER: Zanna.String.Contains
' COVER: Zanna.String.IndexOf
' COVER: Zanna.String.IndexOfFrom
' COVER: Zanna.String.Left
' COVER: Zanna.String.Mid
' COVER: Zanna.String.MidLen
' COVER: Zanna.String.PadLeft
' COVER: Zanna.String.PadRight
' COVER: Zanna.String.Repeat
' COVER: Zanna.String.Replace
' COVER: Zanna.String.Right
' COVER: Zanna.String.Split
' COVER: Zanna.String.StartsWith
' COVER: Zanna.String.Substring
' COVER: Zanna.String.ToLower
' COVER: Zanna.String.ToUpper
' COVER: Zanna.String.Trim
' COVER: Zanna.String.TrimEnd
' COVER: Zanna.String.TrimStart
' COVER: Zanna.Core.Object.Equals
' COVER: Zanna.Core.Object.HashCode
' COVER: Zanna.Core.Object.ToString

SUB AssertApprox(actual AS DOUBLE, expected AS DOUBLE, eps AS DOUBLE, msg AS STRING)
    IF Zanna.Math.Abs(actual - expected) > eps THEN
        Zanna.Core.Diagnostics.Assert(FALSE, msg)
    END IF
END SUB

DIM s AS STRING
s = "  AbCd  "
Zanna.Core.Diagnostics.Assert(("".IsEmpty), "str.empty")
Zanna.Core.Diagnostics.AssertEq(("abcd").Length, 4, "str.len")

Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.Trim(s), "AbCd", "str.trim")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.TrimStart(s), "AbCd  ", "str.trimstart")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.TrimEnd(s), "  AbCd", "str.trimend")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.Left("abcd", 2), "ab", "str.left")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.Right("abcd", 2), "cd", "str.right")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.Mid("abcd", 2), "bcd", "str.mid")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.MidLen("abcd", 2, 2), "bc", "str.midlen")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.Substring("abcd", 1, 2), "bc", "str.substr")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.Concat("ab", "cd"), "abcd", "str.concat")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.ToUpper("aB"), "AB", "str.upper")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.ToLower("aB"), "ab", "str.lower")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.Replace("a-b-a", "-", "+"), "a+b+a", "str.replace")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.PadLeft("7", 3, "0"), "007", "str.padleft")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.PadRight("7", 3, "."), "7..", "str.padright")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.Repeat("ab", 3), "ababab", "str.repeat")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.Reverse("abc"), "cba", "str.flip")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.Chr(66), "B", "str.chr")
Zanna.Core.Diagnostics.AssertEq(Zanna.String.Asc("A"), 65, "str.asc")
Zanna.Core.Diagnostics.AssertEq(Zanna.String.IndexOf("abcd", "cd"), 3, "str.indexof")
Zanna.Core.Diagnostics.AssertEq(Zanna.String.IndexOfFrom("ababa", 2, "ba"), 2, "str.indexoffrom")
Zanna.Core.Diagnostics.AssertEq(Zanna.String.Count("abab", "ab"), 2, "str.count")
Zanna.Core.Diagnostics.Assert(Zanna.String.Contains("hello", "ell"), "str.has")
Zanna.Core.Diagnostics.Assert(Zanna.String.StartsWith("hello", "he"), "str.startswith")
Zanna.Core.Diagnostics.Assert(Zanna.String.EndsWith("hello", "lo"), "str.endswith")
Zanna.Core.Diagnostics.AssertEq(Zanna.String.Compare("a", "b"), -1, "str.cmp")
Zanna.Core.Diagnostics.AssertEq(Zanna.String.CompareIgnoreCase("A", "a"), 0, "str.cmpnocase")

DIM parts AS Zanna.Collections.Seq
parts = Zanna.String.Split("a,b,c", ",")
Zanna.Core.Diagnostics.AssertEq(parts.Count, 3, "str.split.len")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(parts.Get(0)), "a", "str.split.get")

DIM objA AS Zanna.Collections.List
DIM objB AS Zanna.Collections.List
objA = NEW Zanna.Collections.List()
objB = NEW Zanna.Collections.List()
Zanna.Core.Diagnostics.Assert(Zanna.Core.Object.Equals(objA, objA), "obj.equals.self")
Zanna.Core.Diagnostics.Assert(Zanna.Core.Object.Equals(objA, objB) = FALSE, "obj.equals.other")
Zanna.Core.Diagnostics.Assert(Zanna.Core.Object.ToString(objA) <> "", "obj.tostring")
Zanna.Core.Diagnostics.Assert(Zanna.Core.Object.HashCode(objA) <> 0, "obj.hash")

PRINT "RESULT: ok"
END
