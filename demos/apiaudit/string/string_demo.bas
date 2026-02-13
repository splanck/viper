' Viper.String API Audit - Comprehensive Demo
' Tests all Viper.String functions

PRINT "=== Viper.String API Audit ==="

' --- get_Length ---
PRINT "--- get_Length ---"
PRINT Viper.String.get_Length("hello")
PRINT Viper.String.get_Length("")
PRINT Viper.String.get_Length("Hello, World!")

' --- get_IsEmpty ---
PRINT "--- get_IsEmpty ---"
PRINT Viper.String.get_IsEmpty("")
PRINT Viper.String.get_IsEmpty("x")

' --- Trim / TrimStart / TrimEnd ---
PRINT "--- Trim ---"
PRINT "["; Viper.String.Trim("  hello  "); "]"
PRINT "["; Viper.String.TrimStart("  hello  "); "]"
PRINT "["; Viper.String.TrimEnd("  hello  "); "]"

' --- ToUpper / ToLower ---
PRINT "--- ToUpper / ToLower ---"
PRINT Viper.String.ToUpper("hello world")
PRINT Viper.String.ToLower("HELLO WORLD")

' --- Left / Right ---
PRINT "--- Left / Right ---"
PRINT Viper.String.Left("Hello, World!", 5)
PRINT Viper.String.Right("Hello, World!", 6)

' --- Mid / MidLen / Substring ---
PRINT "--- Mid / MidLen / Substring ---"
PRINT Viper.String.Mid("Hello, World!", 8)
PRINT Viper.String.MidLen("Hello, World!", 1, 5)
PRINT Viper.String.Substring("Hello, World!", 7, 5)

' --- IndexOf / IndexOfFrom / LastIndexOf ---
PRINT "--- IndexOf / IndexOfFrom / LastIndexOf ---"
PRINT "IndexOf 'world': "; Viper.String.IndexOf("hello world", "world")
PRINT "IndexOf 'xyz': "; Viper.String.IndexOf("hello world", "xyz")
PRINT "IndexOfFrom: "; Viper.String.IndexOfFrom("hello world hello", 6, "hello")
PRINT "LastIndexOf: "; Viper.String.LastIndexOf("hello world hello", "hello")

' --- Has / StartsWith / EndsWith ---
PRINT "--- Has / StartsWith / EndsWith ---"
PRINT "Has 'world': "; Viper.String.Has("hello world", "world")
PRINT "Has 'xyz': "; Viper.String.Has("hello world", "xyz")
PRINT "StartsWith 'hello': "; Viper.String.StartsWith("hello world", "hello")
PRINT "StartsWith 'world': "; Viper.String.StartsWith("hello world", "world")
PRINT "EndsWith 'world': "; Viper.String.EndsWith("hello world", "world")
PRINT "EndsWith 'hello': "; Viper.String.EndsWith("hello world", "hello")

' --- Replace ---
PRINT "--- Replace ---"
PRINT Viper.String.Replace("hello world", "world", "viper")
PRINT Viper.String.Replace("aaa", "a", "bb")

' --- Split / Join ---
PRINT "--- Split / Join ---"
DIM parts AS Viper.Collections.Seq
parts = Viper.String.Split("one,two,three", ",")
PRINT "Split count: "; parts.Len
DIM sp0 AS OBJECT
sp0 = parts.Get(0)
PRINT "Part 0: "; sp0
DIM sp1 AS OBJECT
sp1 = parts.Get(1)
PRINT "Part 1: "; sp1
DIM sp2 AS OBJECT
sp2 = parts.Get(2)
PRINT "Part 2: "; sp2
PRINT "Join: "; Viper.String.Join("-", parts)

' --- Flip ---
PRINT "--- Flip ---"
PRINT Viper.String.Flip("abcde")
PRINT Viper.String.Flip("racecar")

' --- Repeat ---
PRINT "--- Repeat ---"
PRINT Viper.String.Repeat("ab", 3)
PRINT Viper.String.Repeat("x", 5)

' --- PadLeft / PadRight ---
PRINT "--- PadLeft / PadRight ---"
PRINT "["; Viper.String.PadLeft("42", 6, "0"); "]"
PRINT "["; Viper.String.PadRight("hi", 6, "."); "]"

' --- Count ---
PRINT "--- Count ---"
PRINT "Count 'a' in 'banana': "; Viper.String.Count("banana", "a")
PRINT "Count 'na' in 'banana': "; Viper.String.Count("banana", "na")
PRINT "Count 'z' in 'hello': "; Viper.String.Count("hello", "z")

' --- Cmp / CmpNoCase / Equals ---
PRINT "--- Cmp / CmpNoCase / Equals ---"
PRINT "Cmp apple/banana: "; Viper.String.Cmp("apple", "banana")
PRINT "Cmp banana/apple: "; Viper.String.Cmp("banana", "apple")
PRINT "Cmp apple/apple: "; Viper.String.Cmp("apple", "apple")
PRINT "CmpNoCase Hello/hello: "; Viper.String.CmpNoCase("Hello", "hello")
PRINT "Equals abc/abc: "; Viper.String.Equals("abc", "abc")
PRINT "Equals abc/xyz: "; Viper.String.Equals("abc", "xyz")

' --- Asc / Chr ---
PRINT "--- Asc / Chr ---"
PRINT "Asc('A'): "; Viper.String.Asc("A")
PRINT "Asc('a'): "; Viper.String.Asc("a")
PRINT "Chr(65): "; Viper.String.Chr(65)
PRINT "Chr(97): "; Viper.String.Chr(97)

' --- Concat ---
PRINT "--- Concat ---"
PRINT Viper.String.Concat("Hello, ", "World!")

' --- Capitalize / Title ---
PRINT "--- Capitalize / Title ---"
PRINT Viper.String.Capitalize("hello world")
PRINT Viper.String.Title("hello world foo")

' --- RemovePrefix / RemoveSuffix ---
PRINT "--- RemovePrefix / RemoveSuffix ---"
PRINT Viper.String.RemovePrefix("HelloWorld", "Hello")
PRINT Viper.String.RemovePrefix("HelloWorld", "Bye")
PRINT Viper.String.RemoveSuffix("HelloWorld", "World")
PRINT Viper.String.RemoveSuffix("HelloWorld", "Bye")

' --- TrimChar ---
PRINT "--- TrimChar ---"
PRINT Viper.String.TrimChar("***hello***", "*")
PRINT Viper.String.TrimChar("xxhelloxx", "x")

' --- Slug ---
PRINT "--- Slug ---"
PRINT Viper.String.Slug("Hello, World!  How Are You?")
PRINT Viper.String.Slug("  The Quick Brown Fox  ")

' --- CamelCase / PascalCase / SnakeCase / KebabCase / ScreamingSnake ---
PRINT "--- Case Conversions ---"
PRINT "CamelCase: "; Viper.String.CamelCase("hello world")
PRINT "PascalCase: "; Viper.String.PascalCase("hello world")
PRINT "SnakeCase: "; Viper.String.SnakeCase("helloWorld")
PRINT "KebabCase: "; Viper.String.KebabCase("helloWorld")
PRINT "ScreamingSnake: "; Viper.String.ScreamingSnake("helloWorld")

' --- Levenshtein ---
PRINT "--- Levenshtein ---"
PRINT "kitten/sitting: "; Viper.String.Levenshtein("kitten", "sitting")
PRINT "hello/hello: "; Viper.String.Levenshtein("hello", "hello")
PRINT "empty/abc: "; Viper.String.Levenshtein("", "abc")

' --- Jaro / JaroWinkler ---
PRINT "--- Jaro / JaroWinkler ---"
PRINT "Jaro martha/marhta: "; Viper.String.Jaro("martha", "marhta")
PRINT "JaroWinkler martha/marhta: "; Viper.String.JaroWinkler("martha", "marhta")
PRINT "Jaro hello/hello: "; Viper.String.Jaro("hello", "hello")

' --- Hamming ---
PRINT "--- Hamming ---"
PRINT "karolin/kathrin: "; Viper.String.Hamming("karolin", "kathrin")
PRINT "abc/abc: "; Viper.String.Hamming("abc", "abc")
PRINT "abc/abcd (diff len): "; Viper.String.Hamming("abc", "abcd")

PRINT "=== String Demo Complete ==="
END
